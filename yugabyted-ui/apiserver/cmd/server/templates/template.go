package templates

import (
	"errors"
	"html/template"
	"io"

	"github.com/labstack/echo/v4"
)

type Template struct {
	templates map[string]*template.Template
}

func NewTemplate() *Template {
	return &Template{
		templates: make(map[string]*template.Template),
	}
}

func (t *Template) Render(w io.Writer, html_name string, data interface{}, c echo.Context) error {
	if tmpl, exist := t.templates[html_name]; exist { //Check existence of the t.templates[html_name]
		return tmpl.Execute(w, data) // ** It wll execute the map[string]interface{} data
	} else {
		return errors.New("There is no " + html_name + " in Template map.")
	}

}

func (tmpl *Template) Add(html_name string, template *template.Template) {
	tmpl.templates[html_name] = template
}
