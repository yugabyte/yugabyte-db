package license

import (
	"encoding/base64"
	"encoding/json"
	"time"
)

// Data is the data we expect in the license
type Data struct {
	CustomerEmail string    `json:"customer_email"`
	CreateTime    time.Time `json:"create_time"`
}

// SetTime will load the current time into "data". Duration as -1 to remove
// the monotonic clock that time.Now() adds by default
func (d *Data) SetTime() {
	d.CreateTime = time.Now().Truncate(time.Duration(-1))
}

// Encode the data into base64 by first converting into json
func (d Data) Encode() (string, error) {
	jData, err := json.Marshal(d)
	if err != nil {
		return "", err
	}

	return base64.StdEncoding.EncodeToString(jData), nil
}

// Decode takes a base64 string, decodes it, and the loads the json into Data
func Decode(str string) (d Data, err error) {
	jData, err := base64.StdEncoding.DecodeString(str)
	if err != nil {
		return
	}

	err = json.Unmarshal(jData, &d)
	return
}
