package license

import (
	"testing"
)

func TestEncodeDecodeData(t *testing.T) {
	d := Data{
		CustomerEmail: "customer@yugabyte.com",
	}
	d.SetTime()
	encodedStr, err := d.Encode()
	if err != nil {
		t.Errorf("failed to encode data: %s", err)
	}

	dd, err := Decode(encodedStr)
	if err != nil {
		t.Errorf("failed to decode data")
	}
	if d.CustomerEmail != dd.CustomerEmail {
		t.Errorf("email did not match after encode/decode %s:%s", d.CustomerEmail, dd.CustomerEmail)
	}

	// Ensure we compare strings without the monotonic clock component, which Now() will insert
	dateFmt := "2006-01-02 15:04:05.999999999 -0700 MST"
	if d.CreateTime.Format(dateFmt) != dd.CreateTime.Format(dateFmt) {
		t.Errorf("create time did not match after encode/decode %s:%s", d.CreateTime, dd.CreateTime)
	}
}
