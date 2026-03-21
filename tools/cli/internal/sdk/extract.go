package sdk

import (
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"regexp"
)

var versionPattern = regexp.MustCompile(`set\(FROTH_VERSION\s+"([^"]+)"`)

func FrothHome() (string, error) {
	if home := os.Getenv("FROTH_HOME"); home != "" {
		return home, nil
	}

	home, err := os.UserHomeDir()
	if err != nil {
		return "", fmt.Errorf("home dir: %w", err)
	}

	return filepath.Join(home, ".froth"), nil
}

func SDKPath(version string) (string, error) {
	frothHome, err := FrothHome()
	if err != nil {
		return "", err
	}
	return sdkPathForHome(frothHome, version), nil
}

func EnsureSDK() (string, error) {
	kernelRoot, err := embeddedKernelFS()
	if err != nil {
		return "", err
	}

	version, err := versionFromFS(kernelRoot)
	if err != nil {
		return "", err
	}

	frothHome, err := FrothHome()
	if err != nil {
		return "", err
	}

	return ensureSDKFromFS(kernelRoot, frothHome, version)
}

func ExtractFS(fsys fs.FS, destDir string) error {
	return fs.WalkDir(fsys, ".", func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if path == "." {
			return nil
		}

		destPath := filepath.Join(destDir, filepath.FromSlash(path))
		if d.IsDir() {
			return os.MkdirAll(destPath, 0755)
		}

		info, err := d.Info()
		if err != nil {
			return fmt.Errorf("stat %s: %w", path, err)
		}

		if err := os.MkdirAll(filepath.Dir(destPath), 0755); err != nil {
			return fmt.Errorf("create dir for %s: %w", path, err)
		}

		src, err := fsys.Open(path)
		if err != nil {
			return fmt.Errorf("open %s: %w", path, err)
		}

		mode := fs.FileMode(0644)
		if perm := info.Mode().Perm(); perm != 0 {
			mode = perm
		}

		dst, err := os.OpenFile(destPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, mode)
		if err != nil {
			_ = src.Close()
			return fmt.Errorf("create %s: %w", destPath, err)
		}

		if _, err := io.Copy(dst, src); err != nil {
			_ = dst.Close()
			_ = src.Close()
			return fmt.Errorf("write %s: %w", destPath, err)
		}

		if err := dst.Close(); err != nil {
			_ = src.Close()
			return fmt.Errorf("close %s: %w", destPath, err)
		}

		if err := src.Close(); err != nil {
			return fmt.Errorf("close %s: %w", path, err)
		}

		return nil
	})
}

func embeddedKernelFS() (fs.FS, error) {
	kernelRoot, err := fs.Sub(KernelFS, kernelDir)
	if err != nil {
		return nil, fmt.Errorf("open embedded kernel: %w", err)
	}
	return kernelRoot, nil
}

func versionFromFS(fsys fs.FS) (string, error) {
	data, err := fs.ReadFile(fsys, "CMakeLists.txt")
	if err != nil {
		return "", fmt.Errorf("read embedded CMakeLists.txt: %w", err)
	}

	matches := versionPattern.FindSubmatch(data)
	if len(matches) != 2 {
		return "", fmt.Errorf("parse FROTH_VERSION from embedded CMakeLists.txt")
	}

	return string(matches[1]), nil
}

func ensureSDKFromFS(fsys fs.FS, frothHome string, version string) (string, error) {
	if version == "" {
		return "", fmt.Errorf("sdk version is empty")
	}

	destDir := sdkPathForHome(frothHome, version)
	if sdkReady(destDir) {
		return destDir, nil
	}

	parentDir := filepath.Dir(destDir)
	if err := os.MkdirAll(parentDir, 0755); err != nil {
		return "", fmt.Errorf("create sdk cache dir: %w", err)
	}

	tempDir, err := os.MkdirTemp(parentDir, ".froth-sdk-*")
	if err != nil {
		return "", fmt.Errorf("create sdk temp dir: %w", err)
	}

	if err := ExtractFS(fsys, tempDir); err != nil {
		_ = os.RemoveAll(tempDir)
		return "", fmt.Errorf("extract embedded sdk: %w", err)
	}

	if err := os.Rename(tempDir, destDir); err != nil {
		if sdkReady(destDir) {
			_ = os.RemoveAll(tempDir)
			return destDir, nil
		}
		_ = os.RemoveAll(tempDir)
		return "", fmt.Errorf("activate sdk cache: %w", err)
	}

	return destDir, nil
}

func sdkPathForHome(frothHome string, version string) string {
	return filepath.Join(frothHome, "sdk", "froth-"+version)
}

func sdkReady(dir string) bool {
	if _, err := os.Stat(filepath.Join(dir, "CMakeLists.txt")); err != nil {
		return false
	}
	if _, err := os.Stat(filepath.Join(dir, "src", "froth_vm.h")); err != nil {
		return false
	}
	return true
}
