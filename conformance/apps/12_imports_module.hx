config { database: sqlite }

slice Producto {
  field nombre: string
  field precio: float
  action Crear() { print("producto") }
  validate nombre { required: true }
  validate precio { required: true, min: 0 }
}

api ProductosApi {
  route "/api/productos"     GET  -> Producto.getAllAsJSON
  route "/api/productos"     POST -> Producto.Crear
  route "/api/productos/:id" DELETE -> Producto.deleteRecord
}
