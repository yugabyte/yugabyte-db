package checks

import (
	"fmt"
	"time"

	"github.com/spf13/viper"
)

var Prometheus = &prometheusCheck{
	"prometheus",
	true,
}

type prometheusCheck struct {
	name							string
	skipAllowed				bool
}

func (p prometheusCheck) Name() string {
	return p.name
}

func (p prometheusCheck) SkipAllowed() bool {
	return p.skipAllowed
}

func (p prometheusCheck) Execute() Result {
	res := Result {
		Check: p.name,
		Status: StatusPassed,
	}

	// Checks for scrape interval/timeout difference
	intervalTime, err1 := time.ParseDuration(viper.GetString("prometheus.scrapeInterval"))
	timeoutTime, err2 := time.ParseDuration(viper.GetString("prometheus.scrapeTimeout"))
	if err1 != nil || err2 != nil {
		res.Status = StatusCritical
		res.Error = fmt.Errorf(
			"couldn't parse either scrapeInterval: %s or scrapeTimeout: %s to duration. " +
			"check https://pkg.go.dev/time#ParseDuration for appropriate syntax",
			viper.GetString("prometheus.scrapeInterval"),
			viper.GetString("prometheus.scrapeTimeout"),
		)
		return res
	}

	if intervalTime < timeoutTime {
		res.Status = StatusCritical
		res.Error = fmt.Errorf(
			"prometheus scrape interval can not be less than scrape timeout",
		)
		return res
	}

	// Checks for HTTPS connection certs
	if viper.GetBool("prometheus.enableHttps") {
		cert := viper.GetString("prometheus.httpsCertPath")
		key := viper.GetString("prometheus.httpsKeyPath")
		if (len(cert) == 0 && len(key) != 0) || (len(cert) != 0 && len(key) == 0) {
			res.Status = StatusCritical
			res.Error = fmt.Errorf("need to either set both cert: %s and key: %s or neither", cert, key)
			return res
		}
	}
	return res
}
