//go:build !nolicense

package pubkey

import (
	"crypto"
	"crypto/dsa"
	"crypto/ecdsa"
	"crypto/rsa"
	"crypto/x509"
	_ "embed" // We are using embed
	"encoding/pem"
)

// ValidationRequired shows if validation needs to be performed based on the build type.
const ValidationRequired bool = true

//go:embed license.pub
var key string

var pubkey any // We expect pubKey to get populated by parseKey during init()

func parseKey() {
	block, _ := pem.Decode([]byte(key))
	if block == nil {
		panic("invalid public key in license.pub")
	}
	pk, err := x509.ParsePKIXPublicKey(block.Bytes)
	if err != nil {
		panic(err)
	}
	pubkey = pk
}

// Validate the hash against the signature using the previously parsed public key from license.pub
// DSA keys are not yet supported
// RSA keys must have a SHA256 hash
func Validate(hash, signature []byte) bool {
	// Now, actually do the validation
	switch pk := pubkey.(type) {
	case *rsa.PublicKey:
		err := rsa.VerifyPKCS1v15(pk, crypto.SHA256, hash, signature)
		return err == nil
	case *dsa.PublicKey:
		panic("dsa key not support")
	case *ecdsa.PublicKey:
		return ecdsa.VerifyASN1(pk, hash, signature)
	default:
		panic("unknown public key type")
	}
}

func init() {
	parseKey()
}
