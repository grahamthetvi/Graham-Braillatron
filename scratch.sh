#!/bin/bash
cp /lib/systemd/system/ifup@.service /etc/systemd/system/ifup@.service
sed -i '/^Before=/d' /etc/systemd/system/ifup@.service
systemctl daemon-reload
