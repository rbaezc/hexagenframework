config { database: sqlite }
slice Item {
    field nombre: string
    field precio: int
    action Crear() { print("item") }
}
view Home { title "SQLite" }
api Web {
    route "/" GET -> Home
    route "/api/items" POST -> Item.Crear
}
