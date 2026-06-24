config {
    database: sqlite
    frontend: vanilla
    target: web
}

slice Notificador {
    field item: string
    
    action EnviarAlerta() {
        SMTPClient.sendEmail("localhost", 25, "alert@hexagen.io", "admin@hexagen.io", "Alerta de Sistema", "Ocurrio un evento importante en el inventario.")
        print("Correo de alerta enviado mediante SMTP de C++!")
    }
}
