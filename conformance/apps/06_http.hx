config { http: true }
slice Externo {
    field x: int
    action Llamar() { cpp "auto r = http_get(\"https://api.github.com/zen\"); std::cout << r.status << std::endl;" }
}
view Home { title "HTTP" }
api Web {
    route "/" GET -> Home
    route "/api/call" POST -> Externo.Llamar
}
