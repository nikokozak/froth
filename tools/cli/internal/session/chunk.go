package session

import (
	"fmt"
	"strings"

	"github.com/nikokozak/froth/tools/cli/internal/protocol"
)

const maxEvalSource = protocol.MaxPayload - 3

type chunkScanner struct {
	bracketDepth  int
	colonDepth    int
	commentDepth  int
	inString      bool
	escapeNext    bool
	inLineComment bool
}

func (s *chunkScanner) topLevel() bool {
	return s.bracketDepth == 0 &&
		s.colonDepth == 0 &&
		s.commentDepth == 0 &&
		!s.inString &&
		!s.inLineComment
}

func (s *chunkScanner) scanLine(line string) {
	for i := 0; i < len(line); i++ {
		ch := line[i]

		if s.inLineComment {
			if ch == '\n' {
				s.inLineComment = false
			}
			continue
		}

		if s.commentDepth > 0 {
			switch ch {
			case '(':
				s.commentDepth++
			case ')':
				s.commentDepth--
			}
			continue
		}

		if s.inString {
			if s.escapeNext {
				s.escapeNext = false
				continue
			}
			switch ch {
			case '\\':
				s.escapeNext = true
			case '"':
				s.inString = false
			}
			continue
		}

		switch ch {
		case '\\':
			s.inLineComment = true
		case '(':
			s.commentDepth = 1
		case '"':
			s.inString = true
		case ':':
			s.colonDepth++
		case ';':
			if s.colonDepth > 0 {
				s.colonDepth--
			}
		case '[':
			s.bracketDepth++
		case 'p':
			if i+1 < len(line) && line[i+1] == '[' {
				s.bracketDepth++
				i++
			}
		case ']':
			if s.bracketDepth > 0 {
				s.bracketDepth--
			}
		}
	}

	s.inLineComment = false
}

// ChunkEvalSource splits source on safe top-level line boundaries so each
// chunk fits in one EVAL_REQ payload.
func ChunkEvalSource(source string) ([]string, error) {
	lines := strings.SplitAfter(source, "\n")
	var chunks []string
	var current strings.Builder
	var scanner chunkScanner

	flush := func() error {
		if current.Len() == 0 {
			return nil
		}
		if current.Len() > maxEvalSource {
			return fmt.Errorf("top-level form exceeds link payload limit (%d bytes)", maxEvalSource)
		}
		chunks = append(chunks, current.String())
		current.Reset()
		return nil
	}

	for _, line := range lines {
		if line == "" {
			continue
		}

		if current.Len() > 0 && scanner.topLevel() && current.Len()+len(line) > maxEvalSource {
			if err := flush(); err != nil {
				return nil, err
			}
		}

		current.WriteString(line)
		scanner.scanLine(line)

		if scanner.topLevel() && current.Len() == maxEvalSource {
			if err := flush(); err != nil {
				return nil, err
			}
		}
	}

	if err := flush(); err != nil {
		return nil, err
	}

	if len(chunks) == 0 {
		return []string{""}, nil
	}
	return chunks, nil
}
