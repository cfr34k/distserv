DistServ
========

This is a TCP Server which reads data from stdin and forwards it to each
connected client.

Originally, this was developed as a notification broadcaster for the
[Freifunk-Bot](https://github.com/Bytewerk/freifunk_bot) project, however, as
this is quite versatile, I decided to make it standalone.

Usage
-----

```
distserv [-p <port>] filename
```

You can specify the port number to listen on using the `-p <port>` switch. The
default port is 1234.

The only required argument is the `filename`. This can be either `-` or the
path to a regular file, FIFO or socket. Distserv will read from the file until
EOF is encountered.

If a FIFO or socket is specified, distserv will re-open the file on EOF. This
is useful when a “status broadcaster” should be implemented, using a source
program which might restart occasionally, but you don’t want to drop your
connected clients.

License
-------

```
"THE PIZZA-WARE LICENSE" (derived from "THE BEER-WARE LICENCE"):
<cfr34k@tkolb.de> wrote this file. As long as you retain this notice you can do
whatever you want with this stuff. If we meet some day, and you think this
stuff is worth it, you can buy me a pizza in return. - Thomas Kolb
```

(You may also substitute the pizza with something else if you like.)
