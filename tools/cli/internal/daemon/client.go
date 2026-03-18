package daemon

import (
	"bufio"
	"encoding/json"
	"fmt"
	"net"
	"sync"
	"sync/atomic"
)

// clientResponse is the client-side representation of a JSON-RPC response.
type clientResponse struct {
	Result json.RawMessage `json:"result"`
	Error  *RPCError       `json:"error"`
}

// Client connects to a running daemon over the Unix domain socket.
type Client struct {
	nc     net.Conn
	enc    *json.Encoder
	mu     sync.Mutex // protects enc writes
	nextID uint64

	pending   map[uint64]chan *clientResponse
	pendingMu sync.Mutex

	EventHandler func(method string, params json.RawMessage)
}

// Dial connects to the daemon at the default socket path.
func Dial() (*Client, error) {
	return DialPath(SocketPath())
}

// DialPath connects to the daemon at the given socket path.
func DialPath(socketPath string) (*Client, error) {
	nc, err := net.Dial("unix", socketPath)
	if err != nil {
		return nil, fmt.Errorf("connect to daemon: %w", err)
	}

	c := &Client{
		nc:      nc,
		enc:     json.NewEncoder(nc),
		pending: make(map[uint64]chan *clientResponse),
	}

	go c.readLoop(bufio.NewScanner(nc))

	return c, nil
}

// Close shuts down the client connection.
func (c *Client) Close() error {
	return c.nc.Close()
}

// Call sends a JSON-RPC request and waits for the response.
func (c *Client) Call(method string, params interface{}) (json.RawMessage, error) {
	id := atomic.AddUint64(&c.nextID, 1)

	ch := make(chan *clientResponse, 1)
	c.pendingMu.Lock()
	c.pending[id] = ch
	c.pendingMu.Unlock()

	defer func() {
		c.pendingMu.Lock()
		if c.pending != nil {
			delete(c.pending, id)
		}
		c.pendingMu.Unlock()
	}()

	var rawParams json.RawMessage
	if params != nil {
		var err error
		rawParams, err = json.Marshal(params)
		if err != nil {
			return nil, fmt.Errorf("marshal params: %w", err)
		}
	}

	c.mu.Lock()
	err := c.enc.Encode(&rpcRequest{
		JSONRPC: "2.0",
		Method:  method,
		Params:  rawParams,
		ID:      id,
	})
	c.mu.Unlock()

	if err != nil {
		return nil, fmt.Errorf("send request: %w", err)
	}

	resp := <-ch
	if resp == nil {
		return nil, fmt.Errorf("connection closed")
	}

	if resp.Error != nil {
		return nil, fmt.Errorf("rpc error %d: %s", resp.Error.Code, resp.Error.Message)
	}

	return resp.Result, nil
}

func (c *Client) readLoop(scanner *bufio.Scanner) {
	for scanner.Scan() {
		line := scanner.Bytes()

		// Peek to classify: notification (has "method") vs response (has "id")
		var peek struct {
			Method string `json:"method"`
		}
		json.Unmarshal(line, &peek)

		if peek.Method != "" {
			// JSON-RPC notification (event)
			var notif struct {
				Method string          `json:"method"`
				Params json.RawMessage `json:"params"`
			}
			json.Unmarshal(line, &notif)
			if c.EventHandler != nil {
				c.EventHandler(notif.Method, notif.Params)
			}
		} else {
			// JSON-RPC response
			var resp struct {
				Result json.RawMessage `json:"result"`
				Error  *RPCError       `json:"error"`
				ID     float64         `json:"id"`
			}
			json.Unmarshal(line, &resp)

			id := uint64(resp.ID)
			c.pendingMu.Lock()
			ch, ok := c.pending[id]
			c.pendingMu.Unlock()

			if ok {
				ch <- &clientResponse{
					Result: resp.Result,
					Error:  resp.Error,
				}
			}
		}
	}

	// Connection closed. Signal all pending callers.
	c.pendingMu.Lock()
	for _, ch := range c.pending {
		close(ch)
	}
	c.pending = nil
	c.pendingMu.Unlock()
}

// Convenience methods

func (c *Client) Hello() (*HelloResult, error) {
	raw, err := c.Call("hello", nil)
	if err != nil {
		return nil, err
	}
	var result HelloResult
	if err := json.Unmarshal(raw, &result); err != nil {
		return nil, err
	}
	return &result, nil
}

func (c *Client) Eval(source string) (*EvalResult, error) {
	raw, err := c.Call("eval", &EvalParams{Source: source})
	if err != nil {
		return nil, err
	}
	var result EvalResult
	if err := json.Unmarshal(raw, &result); err != nil {
		return nil, err
	}
	return &result, nil
}

func (c *Client) Info() (*InfoResult, error) {
	raw, err := c.Call("info", nil)
	if err != nil {
		return nil, err
	}
	var result InfoResult
	if err := json.Unmarshal(raw, &result); err != nil {
		return nil, err
	}
	return &result, nil
}

func (c *Client) Reset() (*ResetResult, error) {
	raw, err := c.Call("reset", nil)
	if err != nil {
		return nil, err
	}
	var result ResetResult
	if err := json.Unmarshal(raw, &result); err != nil {
		return nil, err
	}
	return &result, nil
}

func (c *Client) Status() (*StatusResult, error) {
	raw, err := c.Call("status", nil)
	if err != nil {
		return nil, err
	}
	var result StatusResult
	if err := json.Unmarshal(raw, &result); err != nil {
		return nil, err
	}
	return &result, nil
}
