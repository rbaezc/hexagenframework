slice Lead {
    field id: int
    field nombre: string
    action Ver() { print("ver") }
    action Borrar() { print("borrar") }
}
view Home { title "Leads" }
api Web {
    route "/" GET -> Home
    secure route "/api/leads/:id" GET -> Lead.Ver
    route "/api/leads/:id" DELETE -> Lead.Borrar
}
