config { database: postgres }
slice Vuelo {
    field origen: string
    field destino: string
    field precio: float
    field disponible: bool
    action Crear() { print("vuelo") }
    action Actualizar() { print("actualizado") }
}
view Home { title "PostgreSQL" }
api Web {
    route "/" GET -> Home
    route "/api/vuelos" POST -> Vuelo.Crear
    route "/api/vuelos" GET -> Vuelo.getAllAsJSON
    route "/api/vuelos/:id" DELETE -> Vuelo.deleteRecord
}
