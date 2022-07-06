package helpers
import (
    "crypto/rand"
    "math/big"
)

func Random128BitString() (string, error) {
    //Max random value, a 128-bits integer, i.e 2^128 - 1
    max := new(big.Int)
    max.Exp(big.NewInt(2), big.NewInt(128), nil).Sub(max, big.NewInt(1))

    //Generate cryptographically strong pseudo-random between 0 - max
    n, err := rand.Int(rand.Reader, max)
    if err != nil {
        return "", err
    }

    //String representation of n in hex
    nonce := n.Text(16)

    return nonce, nil
}
