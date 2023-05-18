package cmd

type runStep interface {
	RunStep(func() error) error
	StartSection(string)
	EndSection()
}
