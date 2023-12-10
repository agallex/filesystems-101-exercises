package parhash

import (
	"context"
	"net"
	"sync"

	"github.com/pkg/errors"
	"golang.org/x/sync/semaphore"
	"google.golang.org/grpc"

	"fs101ex/pkg/gen/hashsvc"
	"fs101ex/pkg/gen/parhashsvc"
	"fs101ex/pkg/workgroup"
)

type Config struct {
	ListenAddr   string
	BackendAddrs []string
	Concurrency  int
}

type Server struct {
	conf Config

	sem *semaphore.Weighted

	l          net.Listener
	wg         sync.WaitGroup
	stop       context.CancelFunc
	mutex      sync.Mutex
	currClient int
}

func New(conf Config) *Server {
	return &Server{
		conf: conf,
		sem:  semaphore.NewWeighted(int64(conf.Concurrency)),
	}
}

func (s *Server) Start(ctx context.Context) (err error) {
	defer func() { err = errors.Wrap(err, "Start()") }()

    ctx, s.stop = context.WithCancel(ctx)

	s.l, err = net.Listen("tcp", s.conf.ListenAddr)
	if err != nil {
		return err
	}

	srv := grpc.NewServer()
	parhashsvc.RegisterParallelHashSvcServer(srv, s)

	s.wg.Add(2)

	go func() {
		defer s.wg.Done()

		srv.Serve(s.l)
	}()

	go func() {
		defer s.wg.Done()

		<-ctx.Done()
		s.l.Close()
	}()

	return nil
}

func (s *Server) ListenAddr() string {
	return s.l.Addr().String()
}

func (s *Server) Stop() {
	s.stop()
	s.wg.Wait()
}

func (s *Server) ParallelHash(ctx context.Context, req *parhashsvc.ParHashReq) (resp *parhashsvc.ParHashResp, err error) {
	defer func() { err = errors.Wrapf(err, "ParallelHash()") }()

    workers := workgroup.New(workgroup.Config{Sem: s.sem})
    hashes := make([][]byte, len(req.Data))
	backendsCnt := len(s.conf.BackendAddrs)
	cls := make([]hashsvc.HashSvcClient, backendsCnt)
	connections := make([]*grpc.ClientConn, backendsCnt)

	for k, address := range s.conf.BackendAddrs {
		connections[k], err = grpc.Dial(address, grpc.WithInsecure())
		if err != nil {
			return nil, err
		}
		defer connections[k].Close()
		cls[k] = hashsvc.NewHashSvcClient(connections[k])
	}

	for k, buffer := range req.Data {
		currReq := k
		currBuffer := buffer

		workers.Go(ctx, func(ctx context.Context) error {
			s.mutex.Lock()
			currClient := s.currClient
			s.currClient = (s.currClient + 1) % backendsCnt
			s.mutex.Unlock()

			bR, err := cls[currClient].Hash(ctx, &hashsvc.HashReq{Data: currBuffer})
			if err != nil {
				return err
			}
			s.mutex.Lock()
			hashes[currReq] = bR.Hash
			s.mutex.Unlock()
			return nil
		})
	}

	err = workers.Wait()
	if err != nil {
		return nil, err
	}

	return &parhashsvc.ParHashResp{Hashes: hashes}, nil
}