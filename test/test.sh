#!/usr/bin/expect -f

# exp_internal 1
set port /dev/cu.usbmodem1444301
catch { set port $env(PORT) }

set portid [open $port r+]
set timeout 20
set send_slow { 1 .1 }

expect_before {
    timeout { puts "\n\nTimeout detected"; exit 2 }
    eof     { puts "\n\nUnexpected EOD";   exit 1 }
}

fconfigure $portid -mode "9600,n,8,1"
spawn -open $portid


# connect to the target
# and reset the target to get sync
send -s "reset\r"
expect {
    "A>" {
        send "\r"
        expect "A>"
        send "reset\r"
    }
    "MON>" {
        send -s "\r"
        expect "MON>"
        send -s "reset\r"
    }
    "Select: " {
        send "\r"
    }
}
expect {
    "A>" { }
    "Select: " {
        send "\r"
        expect "A>"
    }
}

# create test file
send "pip hello.txt=CON:\r"
sleep 2
expect "\r"
send "Hello, world!\r\x1a"
expect "A>"

# check test file contents
send "type hello.txt\r"
expect "Hello, world!\r"
expect "A>"

send "dump hello.txt\r"
expect "0000 48 65 6C 6C 6F 2C 20 77 6F 72 6C 64 21 0D 1A 1A"
expect "0010 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A"
expect "0020 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A"
expect "0030 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A"
expect "0040 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A"
expect "0050 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A"
expect "0060 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A"
expect "0070 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A 1A"
expect "A>"

send "sdir hello.txt\r"
expect "HELLO    TXT     1k      1 Dir RW      "
expect "A>"

# update test file
send "pip hello.txt=CON:\r"
sleep 2
expect "\r"
send "Good bye, world!\r\x1a"
expect "A>"

# check test file contents
send "type hello.txt\r"
expect "Good bye, world!\r"
expect "A>"

# delete test file
send "era hello.txt\r"
expect "A>"
send "sdir hello.txt\r"
expect "File Not Found."
expect "A>"

send -break
expect "MON>"

send -s "status\r"
expect "MON>"

sleep 1
send -s "cont\r\r"
expect "A>"

send "\r"
expect "A>"

send "sdir\r"
expect "RESET    COM"
send -break
expect "MON>"
send -s "status\r"
expect "MON>"
sleep 1
send -s "cont\r\r"
expect "A>"

send "dir reset.com\r"
expect "A>"

puts "\n\nOK\n"
