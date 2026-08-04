package handlers

import (
	"net/http"

	"github.com/labstack/echo/v4"
)

func IndexHandler(c echo.Context) error {

	return c.Render(http.StatusOK, "index.html", map[string]interface{}{})
}
