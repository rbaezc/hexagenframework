slice Plan {
    field id: int
    field titulo: string
}
slice Suscripcion {
    field id: int
    field plan: relation(Plan)
    action Crear() { print("sub") }
}
view Home { title "Subs" }
api Web {
    route "/" GET -> Home
    route "/api/subs" POST -> Suscripcion.Crear
}
