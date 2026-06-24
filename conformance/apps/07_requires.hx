config { requires: ssl, z }
view Home { title "Req" }
api Web { route "/" GET -> Home }
