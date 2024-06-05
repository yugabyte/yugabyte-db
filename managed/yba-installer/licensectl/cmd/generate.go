package cmd

import (
	"crypto"
	"crypto/rand"
	"crypto/sha256"
	"log"
	"os"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/licensing/license"
	"golang.org/x/crypto/ssh"
)

var (
	customer       string
	outputFile     string
	privateKeyFile string
)

var generateCmd = &cobra.Command{
	Use:   "generate",
	Short: "generate a new license",
	Args:  cobra.NoArgs,
	Run: func(cmd *cobra.Command, args []string) {
		log.Print("generating license for " + customer)
		licData := license.NewDataBuilder().
			Time().
			CustomerEmail(customer).
			Build()

		encoded, err := licData.Encode()
		if err != nil {
			log.Fatal(err)
		}

		key := RetrievePrivateKey()
		sig, err := Sign(encoded, key)
		if err != nil {
			log.Fatal(err)
		}

		lic := license.FromParts(encoded, sig)
		if err := lic.WriteToLocation(outputFile); err != nil {
			log.Fatal(err)
		}

		log.Printf("Created license at %s", outputFile)
	},
}

// Sign the data with the private key by hashing it.
func Sign(d string, key crypto.Signer) ([]byte, error) {
	hash := sha256.Sum256([]byte(d))
	return key.Sign(rand.Reader, hash[:], crypto.SHA256)
}

// RetrievePrivateKey loads the key from the given file
func RetrievePrivateKey() crypto.Signer {
	data, err := os.ReadFile(privateKeyFile)
	if err != nil {
		log.Fatal(err)
	}
	//blocks, _ := pem.Decode(data)
	key, err := ssh.ParseRawPrivateKey(data)
	if err != nil {
		log.Fatal(err)
	}
	return key.(crypto.Signer)
}

func init() {
	generateCmd.Flags().StringVarP(&customer, "customer", "c", "", "Customer name the license is for")
	generateCmd.MarkFlagRequired("customer")
	generateCmd.Flags().StringVarP(&outputFile, "output-file", "o", "yugabyte_anywhere.lic",
		"output license file")
	generateCmd.Flags().StringVarP(&privateKeyFile, "private-key", "p", "id_ecdsa",
		"Path to the private, signing key")

	rootCmd.AddCommand(generateCmd)
}
