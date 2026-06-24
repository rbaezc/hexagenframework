slice Cuenta {
    field email: string
    field password: string
    field edad: int
    action Crear() { print("ok") }
    validate {
        required(email)
        length(password, 8, 64)
        format(email, email)
        min(edad, 18)
        max(edad, 120)
    }
}
view Home { title "Cuenta" }
api Web {
    route "/" GET -> Home
    route "/api/cuentas" POST -> Cuenta.Crear
}
