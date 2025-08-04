package testutils

import (
	"context"
	"io"

	"github.com/aws/aws-sdk-go/aws"
	"github.com/aws/aws-sdk-go/aws/credentials"
	"github.com/aws/aws-sdk-go/aws/session"
	"github.com/aws/aws-sdk-go/service/s3"
)

type Client struct {
	Config awsClientConfig
}

type awsAuthentication struct {
	AccessKeyId     string
	SecretAccessKey string
}

type awsClientConfig struct {
	awsAuthentication
	Ctx    context.Context
	Region string
}

func (c Client) List(bucket, prefix string) ([]string, error) {
	// List files in the s3 bucket matching the prefix
	params := &s3.ListObjectsV2Input{
		Bucket:    aws.String(bucket),
		Prefix:    aws.String(prefix),
		MaxKeys:   aws.Int64(1000), // max number of objects to return
		Delimiter: aws.String("/"),
	}

	s3Client := c.s3Client()
	result, err := s3Client.ListObjectsV2WithContext(c.Config.Ctx, params)
	if err != nil {
		return nil, err
	}

	objects := []string{}
	for {
		for _, obj := range result.CommonPrefixes {
			objects = append(objects, *obj.Prefix)
		}
		for _, obj := range result.Contents {
			objects = append(objects, *obj.Key)
		}

		if *result.IsTruncated {
			params.ContinuationToken = result.NextContinuationToken
			result, err = s3Client.ListObjectsV2WithContext(c.Config.Ctx, params)
			if err != nil {
				return objects, err
			}
		} else {
			break
		}
	}
	return objects, nil
}

func (c Client) Download(bucket, key string) (io.Reader, error) {
	s3Client := c.s3Client()
	result, err := s3Client.GetObjectWithContext(c.Config.Ctx, &s3.GetObjectInput{
		Bucket: aws.String(bucket),
		Key:    aws.String(key),
	})
	if err != nil {
		return nil, err
	}
	return result.Body, nil
}

func (c Client) s3Client() *s3.S3 {
	return s3.New(session.Must(session.NewSession(&aws.Config{
		Region:      aws.String(c.Config.Region),
		Credentials: credentials.NewStaticCredentials(c.Config.AccessKeyId, c.Config.SecretAccessKey, ""),
	})))
}

func NewS3Client() *Client {
	cfg, err := getAwsClientConfig(context.Background(), "us-west-2")
	if err != nil {
		panic(err)
	}
	return &Client{
		Config: cfg,
	}
}

func getAwsClientConfig(ctx context.Context, region string) (cfg awsClientConfig, err error) {
	cfg.Ctx = ctx
	cfg.Region = region
	auth := authFromEnv()
	if auth.AccessKeyId == "" || auth.SecretAccessKey == "" {
		auth, err = authFromBotAccessJson()
		if err != nil {
			return
		}
	}
	cfg.awsAuthentication = auth
	return
}
