slice Mensaje {
    field texto: string
    action Enviar() { print("msg") }
}
view Home { title "Chat" }
api Web {
    use logger
    use cors
    use rate_limit(100, 60)
    route "/" GET -> Home
    route "/api/msg" POST -> Mensaje.Enviar
    websocket "/ws" -> Mensaje.Enviar
}
