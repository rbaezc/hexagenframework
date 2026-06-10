// Archivo: demo.hx

// Define la lógica de negocio y datos
slice Inventario {
    field item: string
    field cantidad: int
    
    action Agregar() {
        print("C++ Core ejecutó: Inventario.Agregar()")
    }
    
    action Eliminar() {
        print("C++ Core ejecutó: Inventario.Eliminar()")
    }
}

// Vista Principal
view Main {
    title "Vortex Control de Inventario"
    input item: string
    input cantidad: int
    button "Guardar Producto en C++" -> Inventario.Agregar
    button "Ir a Configuración" -> Config
    table Inventario -> item, cantidad
}

// Vista Secundaria de Configuración
view Config {
    title "Configuración del Sistema"
    button "Volver al Inventario" -> Main
}

// Define el enrutamiento HTTP REST seguro (Pillar 3)
api Rest {
    secure route "/process" POST -> Inventario.Agregar
    secure route "/eliminar" DELETE -> Inventario.Eliminar
}
