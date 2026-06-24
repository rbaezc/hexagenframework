job Email {
    field to: string
    field intentos: int
    action Run() { cpp "std::cout << \"email \" << to << std::endl;" }
}
slice Disparador {
    field x: int
    action Lanzar() { enqueue Email(to: "a@b.com", intentos: 2) }
}
view Home { title "Jobs" }
api Web {
    route "/" GET -> Home
    route "/api/run" POST -> Disparador.Lanzar
}
