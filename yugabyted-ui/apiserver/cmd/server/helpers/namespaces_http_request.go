package helpers

import (
  "encoding/json"
  "errors"
  "fmt"
  "io/ioutil"
  "net/http"
)

type NamespaceResponse struct {
  UserNamespaces   []NamespacesInfoNamespaceHTTP `json:"User Namespaces"`
  SystemNamespaces []NamespacesInfoNamespaceHTTP `json:"System Namespaces"`
}

type NamespacesInfoNamespaceHTTP struct {
  Name      string `json:"name"`
  ID        string `json:"id"`
  Language  string `json:"language"`
  State     string `json:"state"`
  Colocated string `json:"colocated"`
}

type NamespaceFuture struct {
  NamespaceResponse NamespaceResponse
  Error             error
}

func (h *HelperContainer) GetNamespacesFuture(nodeHost string, future chan NamespaceFuture) {
  namespaceFuture := NamespaceFuture{
    NamespaceResponse: NamespaceResponse{},
    Error:             nil,
  }

  url := fmt.Sprintf("http://%s:%s/api/v1/namespaces", nodeHost, MasterUIPort)
  resp, err := http.Get(url)
  if err != nil {
    namespaceFuture.Error = err
    future <- namespaceFuture
    return
  }
  defer resp.Body.Close()

  if resp.StatusCode != http.StatusOK {
    namespaceFuture.Error = fmt.Errorf("unexpected response status: %s", resp.Status)
    future <- namespaceFuture
    return
  }

  body, err := ioutil.ReadAll(resp.Body)
  if err != nil {
    namespaceFuture.Error = err
    future <- namespaceFuture
    return
  }

  var result map[string]interface{}
  err = json.Unmarshal(body, &result)
  if err != nil {
    namespaceFuture.Error = err
    future <- namespaceFuture
    return
  }

  if val, ok := result["error"]; ok {
    namespaceFuture.Error = errors.New(val.(string))
    future <- namespaceFuture
    return
  }
  err = json.Unmarshal(body, &namespaceFuture.NamespaceResponse)
  namespaceFuture.Error = err

  future <- namespaceFuture
}
