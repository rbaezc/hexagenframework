slice User {
    field email: string
    field password: string
    field rol: string
}
slice Secreto {
    field dato: string
    action Crear() { print("secreto") }
}
view Home { title "Auth" }
api Web {
    route "/" GET -> Home
    secure route "/api/secreto" POST -> Secreto.Crear
}
