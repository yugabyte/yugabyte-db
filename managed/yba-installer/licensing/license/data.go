package license

import (
	"encoding/base64"
	"encoding/json"
	"time"
)

// Current version of the license data.
const licenseVersion = 1

// Data is the data we expect in the license.
type Data struct {
	CustomerEmail string    `json:"customer_email"`
	CreateTime    time.Time `json:"create_time"`
	Version       int       `json:"version"`
}

// SetTime will load the current time into "data". Duration as -1 to remove
// the monotonic clock that time.Now() adds by default.
func (d *Data) SetTime() {
	d.CreateTime = time.Now().Truncate(time.Duration(-1))
}

// Encode the data into base64 by first converting into json.
func (d Data) Encode() (string, error) {
	jData, err := json.Marshal(d)
	if err != nil {
		return "", err
	}

	return base64.StdEncoding.EncodeToString(jData), nil
}

// Decode takes a base64 string, decodes it, and the loads the json into Data.
func Decode(str string) (d Data, err error) {
	jData, err := base64.StdEncoding.DecodeString(str)
	if err != nil {
		return
	}

	err = json.Unmarshal(jData, &d)
	return
}

// DataBuilder builds the yba-installer license data.
type DataBuilder struct {
	data     Data
	complete bool
}

// NewDataBuilder will construct a data builder for us.
func NewDataBuilder() *DataBuilder {
	return &DataBuilder{
		data:     Data{Version: licenseVersion},
		complete: false,
	}
}

// Build will compile and return the data object.
func (d *DataBuilder) Build() Data {
	if d.complete {
		panic("databuilder has already been built")
	}
	d.complete = true
	return d.data
}

// Time adds the current time to the data object.
func (d *DataBuilder) Time() *DataBuilder {
	d.data.SetTime()
	return d
}

// CustomerEmail sets the customer email field.
func (d *DataBuilder) CustomerEmail(email string) *DataBuilder {
	d.data.CustomerEmail = email
	return d
}
