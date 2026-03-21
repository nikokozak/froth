package sdk

import "embed"

const kernelDir = "kernel"

//go:embed all:kernel
var KernelFS embed.FS
