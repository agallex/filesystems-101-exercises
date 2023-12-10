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

	Prom prometheus.Registerer
}

type Server struct {
	conf Config

	sem *semaphore.Weighted

	l          net.Listener
	wg         sync.WaitGroup
	stop       context.CancelFunc
	mutex      sync.Mutex
	currClient int

	nr_requests        prometheus.Counter
    subquery_durations *prometheus.HistogramVec
}

func New(conf Config) *Server {
	return &Server{
		conf: conf,
		sem:  semaphore.NewWeighted(int64(conf.Concurrency)),
		nr_requests: prometheus.NewCounter(prometheus.CounterOpts{
			Namespace: "parhash",
			Name:      "nr_requests",
		}),
		subquery_durations: prometheus.NewHistogramVec(prometheus.HistogramOpts{
			Namespace: "parhash",
			Name:      "subquery_durations",
			Buckets:   prometheus.ExponentialBuckets(0.1, 10*1000, 24),
		}, []string{"backend"}),
	}
}

func (s *Server) Start(ctx context.Context) (err error) {
	defer func() { err = errors.Wrap(err, "Start()") }()

    ctx, s.stop = context.WithCancel(ctx)
    s.conf.Prom.MustRegister(s.nr_requests)
    s.conf.Prom.MustRegister(s.subquery_durations)

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

    s.nr_requests.Inc()

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

			start_time := time.Now()
			bR, err := cls[currClient].Hash(ctx, &hashsvc.HashReq{Data: currBuffer})
			passed_time := time.Since(start_time)

			if err != nil {
				return err
			}

			s.mutex.Lock()
			hashes[currReq] = bR.Hash
			s.mutex.Unlock()

			s.subquery_durations.With(prometheus.Labels{"backend": s.conf.BackendAddrs[currClient]}).Observe(float64(passed_time.Milliseconds()))

			return nil
		})
	}

	err = workers.Wait()
	if err != nil {
		return nil, err
	}

	return &parhashsvc.ParHashResp{Hashes: hashes}, nil
}