package helpers

import (
    "fmt"
    "io"
    "net/http"
    "reflect"
    "strings"
    "testing"
    "testing/synctest"

    "apiserver/cmd/server/logger"
)

type discoveryTransport func(*http.Request) (*http.Response, error)

func (f discoveryTransport) RoundTrip(r *http.Request) (*http.Response, error) {
    return f(r)
}

func TestGetMasterAddressesFuture(t *testing.T) {
    for _, tc := range []struct {
        name         string
        tserverFails bool
        masterFails  bool
    }{
        {name: "tserver succeeds"},
        {name: "tserver succeeds with master failure", masterFails: true},
        {name: "fallback to master", tserverFails: true},
        {name: "both lookups fail", tserverFails: true, masterFails: true},
    } {
        t.Run(tc.name, func(t *testing.T) {
            // These tests replace process-wide configuration and must not run in parallel.
            oldHost, oldMasterPort, oldTserverPort := HOST, MasterUIPort, TserverUIPort
            oldTransport, oldCache := http.DefaultTransport, MasterAddressCache.Get()
            t.Cleanup(func() {
                HOST, MasterUIPort, TserverUIPort = oldHost, oldMasterPort, oldTserverPort
                http.DefaultTransport = oldTransport
                MasterAddressCache.Update(oldCache)
            })
            HOST, MasterUIPort, TserverUIPort = "192.0.2.1", "7000", "9000"
            log, err := logger.NewLogger(logger.Error)
            if err != nil {
                t.Fatal(err)
            }
            defer log.Cleanup()

            // synctest waits for all spawned goroutines to exit and detects blocked sends.
            synctest.Test(t, func(t *testing.T) {
                releaseMaster := make(chan struct{})
                http.DefaultTransport = discoveryTransport(func(
                    r *http.Request,
                ) (*http.Response, error) {
                    var body string
                    switch r.URL.Host {
                    case "192.0.2.1:9000":
                        if tc.tserverFails {
                            return nil, fmt.Errorf("tserver unavailable")
                        }
                        body = `{"master_server_and_type":[{"master_server":"192.0.2.1:7100"}]}`
                    case "192.0.2.1:7000":
                        if !tc.tserverFails {
                            // Let discovery return before the unused master lookup completes.
                            <-releaseMaster
                        }
                        if tc.masterFails {
                            return nil, fmt.Errorf("master unavailable")
                        }
                        body = `{"masters":[{"registration":{` +
                            `"private_rpc_addresses":[{"host":"192.0.2.2","port":7100}]}}]}`
                    default:
                        return nil, fmt.Errorf("unexpected URL: %s", r.URL)
                    }
                    return &http.Response{
                        StatusCode: http.StatusOK,
                        Body:       io.NopCloser(strings.NewReader(body)),
                        Header:     make(http.Header),
                    }, nil
                })

                h := HelperContainer{logger: log}
                future := make(chan MasterAddressesFuture)
                go h.GetMasterAddressesFuture(future)
                result := <-future
                close(releaseMaster)
                if tc.tserverFails && tc.masterFails {
                    if result.Error == nil {
                        t.Fatal("expected an error when both lookups fail")
                    }
                    return
                }
                if result.Error != nil {
                    t.Fatal(result.Error)
                }
                want := []string{"192.0.2.1"}
                if tc.tserverFails {
                    want = []string{"192.0.2.2"}
                }
                if !reflect.DeepEqual(result.HostList, want) {
                    t.Fatalf("master addresses = %v, want %v", result.HostList, want)
                }
            })
        })
    }
}
