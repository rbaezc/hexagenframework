slice Tarea {
    field titulo: string
    field hecha: bool
    action Crear() { print("tarea creada") }
}
view Home { title "Tareas" }
api Web {
    route "/" GET -> Home
    route "/api/tareas" POST -> Tarea.Crear
}
