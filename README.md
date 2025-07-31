<p align="center">
  <a href="https://github.com/ikonopistsev/capstomp/releases"><img src="https://img.shields.io/github/release/ikonopistsev/capstomp.svg?style=for-the-badge" /></a>
  <a href="https://github.com/ikonopistsev/capstomp/actions?query=workflow%3Alinux>"><img src="https://img.shields.io/github/actions/workflow/status/ikonopistsev/capstomp/linux.yaml?branch=master&style=for-the-badge&label=linux" /></a>
  <a href="https://github.com/ikonopistsev/capstomp/blob/master/LICENSE"><img src="https://img.shields.io/github/license/ikonopistsev/capstomp?style=for-the-badge" /></a>
</p>

# capstomp

`capstomp` is a [MySQL](https://en.wikipedia.org/wiki/MySQL) user-defined function ([udf](https://dev.mysql.com/doc/extending-mysql/8.0/en/adding-functions.html)) library for sending messages to message brokers like [ActiveMQ Artemis](https://activemq.apache.org/components/artemis/) or [RabbitMQ](https://en.wikipedia.org/wiki/RabbitMQ) using the [STOMP](https://en.wikipedia.org/wiki/Streaming_Text_Oriented_Messaging_Protocol) protocol (v1.2). Module has very high performance based on pool of persistent tcp connections. This library is similar to [lib_mysqludf_amqp](https://github.com/ssimicro/lib_mysqludf_amqp) which is used to publish messages via AMQP directly from MySQL.

To use with the RabbitMQ, the [STOMP plugin](https://www.rabbitmq.com/stomp.html) is required.

## Requirements

* C++ compiler with C++17 support (tested with gcc and clang) and cmake build tool.
* [MySQL](http://www.mysql.com/) or forks
* [stomptalk](https://github.com/ikonopistsev/stomptalk) STOMP protocol parser library
* [stompconn](https://github.com/ikonopistsev/stompconn) simple STOMP connector
* [libevent](https://github.com/libevent/libevent) event notification library
* [ActiveMQ Artemis](http://activemq.apache.org/components/artemis/) with native STOMP protocol support
* [OR] [RabbitMQ with STOMP plugin](https://www.rabbitmq.com/stomp.html) for protocol support

## Example

### Hello, World!

Publishes a json `['Hello, World!']` to the `udf` exchange on `localhost:61613` with a routing key of `test` as the user `guest` with the password `guest` and vhost `123`. Upon success, the message size is returned.
```
mysql> SELECT capstomp_json('stomp://guest:guest@localhost/123#/exchange/udf', 'test', json_array('Hello, World!'));
```
this example works with [MySQL 8 json functions](https://dev.mysql.com/doc/refman/8.0/en/json-function-reference.html)

You may use any json generator for MySQL. I use [my own](https://github.com/ikonopistsev/capjs) :)
```
mysql> SELECT capstomp_json('stomp://guest:guest@localhost:61613/123#/exchange/udf', 'test', jsarr('Hello, World!'));
```
Publish a string 'text' with custom headers
```
mysql> SELECT capstomp('stomp://guest:guest@localhost:61613/123#/exchange/udf', '', 'text', 'somekey1=1&some-key2=value');
```
Table event's

The following publishes JSON objects representing table rows whenever a row is inserted, updated, or deleted.
```
DROP TABLE IF EXISTS `country`
;
CREATE TABLE `country` (
  `id` int NOT NULL,
  `name` varchar(100) COLLATE utf8_bin DEFAULT NULL,
  `inhabitants` int DEFAULT NULL,
  `continent` varchar(20) COLLATE utf8_bin DEFAULT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COLLATE=utf8_bin
;

DELIMITER ;;

DROP TRIGGER IF EXISTS `country_AFTER_INSERT`;
CREATE DEFINER=`root`@`localhost` TRIGGER `country_AFTER_INSERT` AFTER INSERT ON `country` FOR EACH ROW BEGIN
  	SET @message_size = (
		capstomp_json('stomp://stompdemo:123@localhost:61613/stompdemo#/queue/a1', '', 
			JSON_OBJECT('method', 'create', 'payload', JSON_OBJECT('id', NEW.`id`, 'name', NEW.`name`, 'inhabitants', NEW.`inhabitants`, 'continent', NEW.`continent`))));
END ;;

DROP TRIGGER IF EXISTS `country_AFTER_UPDATE`;
CREATE DEFINER=`root`@`localhost` TRIGGER `country_AFTER_UPDATE` AFTER UPDATE ON `country` FOR EACH ROW BEGIN
	SET @message_size = (
		capstomp_json('stomp://stompdemo:123@localhost:61613/stompdemo#/queue/a1', '', 
			JSON_OBJECT('method', 'modify', 'payload', JSON_OBJECT('id', NEW.`id`, 'name', NEW.`name`, 'inhabitants', NEW.`inhabitants`, 'continent', NEW.`continent`))));
END ;;

DROP TRIGGER IF EXISTS `country_AFTER_DELETE`;
CREATE DEFINER=`root`@`localhost` TRIGGER `country_AFTER_DELETE` AFTER DELETE ON `country` FOR EACH ROW BEGIN
	SET @message_size = (
		capstomp_json('stomp://stompdemo:123@localhost:61613/stompdemo#/queue/a1', '', 
			JSON_OBJECT('method', 'remove', 'payload', JSON_OBJECT('id', OLD.`id`))));
END ;;

DELIMITER ;
```


## API

### `capstomp(uri, routing-key, json-data [, stomp-header-pairs...])`

Sends a `json-data` to the given `destination` on the provided `uri`.

#### Parameters

* `uri` (string). "stomp://guest:guest@localhost/vhost#/stomp_destination/name" [STOMP destination](https://www.rabbitmq.com/stomp.html#d).
* `routing-key` (string). routing key for exchanges, or empty '' [STOMP exchange destination](https://www.rabbitmq.com/stomp.html#d.ed).
* `json-data` (string). The body of the message (typically json but it may any string).
* `stomp-header-pairs` (query pairs like key1=val1&key2=val2 etc). `CONCAT('my_timestamp=', round(unix_timestamp(now(4))*1000), '&some_id=', 42)` will add to STOMP headers `my_timestamp=1599081164296` and `some_id=42`, but you have to be careful with it.

#### Returns

Upon succes, this function returns a number containing the size of sending data.

### `capstomp_json(uri, routing-key, json-data [, stomp-header-pairs...])`

Same as `capstomp` but it add `content-type=application/json` header to each message.

## Building

Build with cmake and system libevent

```
git clone --recurse-submodules https://github.com/ikonopistsev/capstomp.git
$ cd capstomp
$ mkdir b && cd b
$ cmake -DCMAKE_BUILD_TYPE=Release ..
# ccmake .. (if needed)
$ make
```

### Build with static linked libevent

```
...
$ cmake -DCMAKE_BUILD_TYPE=Release -DCAPSTOMP_STATIC_LIBEVENT=ON ..
...
```

Add `-DCAPSTOMP_HAVE_MY_BOOL=ON` if `my_bool` type is present in `mysql.h`

### Installation 

copy `libcapstomp.so` to mysql pugins directory (usually to `/usr/lib/mysql/plugin` or same) then import methods

```
CREATE FUNCTION capstomp RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_json RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_status RETURNS STRING SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_store_erase RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_store_clear RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_timeout RETURNS integer SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_max_pool_count RETURNS integer SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_max_pool_sockets RETURNS integer SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_pool_sockets RETURNS integer SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_verbose RETURNS integer SONAME 'libcapstomp.so';
```

> Discription based on [lib_mysqludf_amqp](https://github.com/ssimicro/lib_mysqludf_amqp)


