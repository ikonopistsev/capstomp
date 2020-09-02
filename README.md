[![linux](https://github.com/ikonopistsev/capstomp/workflows/linux/badge.svg)](https://github.com/ikonopistsev/capstomp/actions?query=workflow%3Alinux)

# capstomp
capstomp is a [MySQL](https://en.wikipedia.org/wiki/MySQL) user-defined function ([udf](https://dev.mysql.com/doc/extending-mysql/8.0/en/adding-functions.html)) library for sending messages to message brokers like [RabbitMQ](https://en.wikipedia.org/wiki/RabbitMQ) or [Apache ActiveMQ](https://en.wikipedia.org/wiki/Apache_ActiveMQ) using the [STOMP](https://en.wikipedia.org/wiki/Streaming_Text_Oriented_Messaging_Protocol) protocol.

To use with the RabbitMQ, the [STOMP plugin](https://www.rabbitmq.com/stomp.html) is required.