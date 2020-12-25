<p align="center">
  <a href="https://github.com/ikonopistsev/capstomp/releases"><img src="https://img.shields.io/github/release/ikonopistsev/capstomp.svg?style=for-the-badge" /></a>
  <a href="https://github.com/ikonopistsev/capstomp/actions?query=workflow%3Alinux>"><img src="https://img.shields.io/github/workflow/status/ikonopistsev/capstomp/linux?label=linux&style=for-the-badge" /></a>
  <a href="https://lgtm.com/projects/g/ikonopistsev/capstomp"><img alt="LGTM Grade" src="https://img.shields.io/lgtm/grade/cpp/github/ikonopistsev/capstomp?style=for-the-badge" /></a>
  <a href="https://github.com/ikonopistsev/capstomp/blob/master/LICENSE"><img src="https://img.shields.io/github/license/ikonopistsev/capstomp?style=for-the-badge" /></a>
</p>

# capstomp

`capstomp` is a [MySQL](https://en.wikipedia.org/wiki/MySQL) user-defined function ([udf](https://dev.mysql.com/doc/extending-mysql/8.0/en/adding-functions.html)) library for sending messages to message brokers like [ActiveMQ Artemis](https://activemq.apache.org/components/artemis/) or [RabbitMQ](https://en.wikipedia.org/wiki/RabbitMQ) using the [STOMP](https://en.wikipedia.org/wiki/Streaming_Text_Oriented_Messaging_Protocol) protocol. Module has very hi performance based on pool of persistent tcp connections.

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
this example works with [MySQL 8 json functions](https://dev.mysql.com/doc/refman/8.0/en/json-functions.html)

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

### Build on CentOS 7

1. You need to install cmake3 from [EPEL](https://fedoraproject.org/wiki/EPEL) repository

```
$ yum install https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
$ yum update
$ yum install cmake3
```

2. You need to install new C++ compiller with C++17 support

First, install the CentOS SCL release file. It is part of the CentOS extras repository and can be installed by running the following command:

```
$ sudo yum install centos-release-scl
```

In this example, we’ll install the Developer Toolset version 9. To do so type the following command on your CentOS 7 terminal:

```
$ sudo yum install devtoolset-9
```

To access GCC version 9, you need to launch a new shell instance using the Software Collection scl tool:

```
$ scl enable devtoolset-9 bash
```

Now if you check the GCC version, you’ll notice that GCC 9 is the default version in your current shell:

```
$ gcc --version

gcc (GCC) 9.3.1 20200408 (Red Hat 9.3.1-2)
```

3. Install libevent and mysql development files

```
$ yum install libevent-devel mariadb-devel
```

4. If you want to build rpm package you need to install rpm-build

```
$ yum install -y rpm-build
```

5. Build

in capstomp directory 

```
$ mkdir b && cd b
```

builing with static dependencies

```
$ cmake3 -DCMAKE_BUILD_TYPE=Release -DCAPSTOMP_HAVE_MY_BOOL=ON -DCAPSTOMP_STATIC_STDCPP=ON -DCPACK_GENERATOR="RPM" ..
```

check the dependencies

```
$ ldd libcapstomp.so 
        linux-vdso.so.1 =>  (0x00007fff209be000)
        libevent-2.0.so.5 => /lib64/libevent-2.0.so.5 (0x00007f7da60b7000)
        libm.so.6 => /lib64/libm.so.6 (0x00007f7da5db5000)
        libc.so.6 => /lib64/libc.so.6 (0x00007f7da59e7000)
        /lib64/ld-linux-x86-64.so.2 (0x00007f7da62ff000)
        libpthread.so.0 => /lib64/libpthread.so.0 (0x00007f7da57cb000)
```

6. Create rpm package

just run cpack3

```
$ cpack3
```

### Build on Debian 9

For Debian 9 I use clang-11 compiller [oficial stable release](https://apt.llvm.org/)

1. Install repository key and apt https support.

```
$ wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key|sudo apt-key add -
$ apt install apt-transport-https
```

2. Add repository path to /etc/apt/sources.list and update apt

```
# llvm 11 
deb http://apt.llvm.org/stretch/ llvm-toolchain-stretch-11 main
deb-src http://apt.llvm.org/stretch/ llvm-toolchain-stretch-11 main
```

3. Run update

```
$ apt update
```

4. Install dependencies

```
apt install default-libmysqlclient-dev libevent-dev
```

5. Insall utilities

```
apt install cmake cmake-curses-gui git wget build-essential
```

6. Install clang and libc++11

```
$ apt install clang-11 lldb-11 lld-11 libc++-11-dev libc++abi-11-dev
```

7. Setup compiller

```
$ export CC=clang-11
$ export CXX=clang++-11
```

8. Configure project

```
cmake -DCAPSTOMP_CLANG_LIBCXX=ON -DCAPSTOMP_HAVE_MY_BOOL=ON -DCMAKE_BUILD_TYPE=Release -DCPACK_GENERATOR=DEB ..
```

9. Create deb package

```
cpack
```

### Installation 

copy `libcapstomp.so` to mysql pugins directory (usually to `/usr/lib/mysql/plugin` or same) then import methods

```
CREATE FUNCTION capstomp RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_json RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_status RETURNS STRING SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_store_erase RETURNS INTEGER SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_read_timeout RETURNS integer SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_max_pool_count RETURNS integer SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_max_pool_sockets RETURNS integer SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_pool_sockets RETURNS integer SONAME 'libcapstomp.so';
CREATE FUNCTION capstomp_verbose RETURNS integer SONAME 'libcapstomp.so';
```

> Discription based on [lib_mysqludf_amqp](https://github.com/ssimicro/lib_mysqludf_amqp)

