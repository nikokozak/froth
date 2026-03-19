package session

import (
	"strings"
	"testing"
)

func TestChunkEvalSourceKeepsContent(t *testing.T) {
	source := strings.Repeat(": inc 1 + ;\n", 40)

	chunks, err := ChunkEvalSource(source)
	if err != nil {
		t.Fatalf("ChunkEvalSource failed: %v", err)
	}
	if len(chunks) < 2 {
		t.Fatalf("expected multiple chunks, got %d", len(chunks))
	}
	if strings.Join(chunks, "") != source {
		t.Fatal("chunk join mismatch")
	}
	for _, chunk := range chunks {
		if len(chunk) > maxEvalSource {
			t.Fatalf("chunk too large: %d", len(chunk))
		}
	}
}

func TestChunkEvalSourceIgnoresCommentSyntaxNoise(t *testing.T) {
	source := strings.Repeat("\\ [ : ; ]\n1 2 +\n", 20)

	chunks, err := ChunkEvalSource(source)
	if err != nil {
		t.Fatalf("ChunkEvalSource failed: %v", err)
	}
	if strings.Join(chunks, "") != source {
		t.Fatal("chunk join mismatch")
	}
}

func TestChunkEvalSourceIgnoresStringSyntaxNoise(t *testing.T) {
	source := strings.Repeat("\"[ : ; ]\" .\n", 20)

	chunks, err := ChunkEvalSource(source)
	if err != nil {
		t.Fatalf("ChunkEvalSource failed: %v", err)
	}
	if strings.Join(chunks, "") != source {
		t.Fatal("chunk join mismatch")
	}
}

func TestChunkEvalSourceHandlesPatternSyntax(t *testing.T) {
	source := strings.Repeat("p[a [b] c] drop\n", 20)

	chunks, err := ChunkEvalSource(source)
	if err != nil {
		t.Fatalf("ChunkEvalSource failed: %v", err)
	}
	if strings.Join(chunks, "") != source {
		t.Fatal("chunk join mismatch")
	}
}

func TestChunkEvalSourceHandlesNestedCommentsAndEscapes(t *testing.T) {
	source := strings.Repeat("( outer ( inner [ : ; ] ) ) \"\\\"[x]\\\"\" .\n", 20)

	chunks, err := ChunkEvalSource(source)
	if err != nil {
		t.Fatalf("ChunkEvalSource failed: %v", err)
	}
	if strings.Join(chunks, "") != source {
		t.Fatal("chunk join mismatch")
	}
}

func TestChunkEvalSourceRejectsOversizedTopLevelForm(t *testing.T) {
	source := strings.Repeat("1", maxEvalSource+1) + "\n"

	_, err := ChunkEvalSource(source)
	if err == nil {
		t.Fatal("expected oversized form error")
	}
}
