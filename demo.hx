// Archivo: demo.hx

// Define la lógica de negocio y datos
slice Inventario {
    field item: string
    field cantidad: int
    
    action Agregar() {
        print("C++ Core ejecutó: Inventario.Agregar()")
        enqueue ProcessOrder(orderId: 42)
    }
    
    action Eliminar() {
        print("C++ Core ejecutó: Inventario.Eliminar()")
    }
}

// Job de Segundo Plano
job ProcessOrder {
    field orderId: int
    
    action Run() {
        print("Trabajador ejecutando ProcessOrder para orderId")
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
    use cors
    use rate_limit(3, 10)
    
    secure route "/process" POST -> Inventario.Agregar
    secure route "/eliminar" DELETE -> Inventario.Eliminar
}
