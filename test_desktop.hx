config {
    database: sqlite
    frontend: vanilla
    target: desktop
}

slice Inventario {
    field item: string
    field cantidad: int
    
    action Agregar() {
        print("C++ Core ejecutó: Inventario.Agregar()")
    }
}

view Main {
    title "Vortex Control de Inventario"
    input item: string
    input cantidad: int
    button "Guardar Producto en C++" -> Inventario.Agregar
}
