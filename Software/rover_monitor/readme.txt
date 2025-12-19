To build the rover_monitor simply type make. Its best to test it out from the command line first. 
Below are the steps to add the executable to systemctl so it runs from raspberry pi boot up.

1. build and test the command line.

2. sudo cp rover_monitor_12 /usr/local/bin/rover_monitor_12

3. Make it executable using the chmod command.
   sudo chmod +x /usr/local/bin/rover_monitor_12

4. Create a new service unit file in the /etc/systemd/system/ directory. 
   sudo nano /etc/systemd/system/rover_monitor.service

5. Add the following file content to /etc/systemd/system/rover_monitor.service

[Unit]
Description=ip2oled monitor bonnet
After=network.target

[Service]
ExecStart=/usr/local/bin/rover_monitor_12
User=root
Group=root
Restart=on-failure

[Install]
WantedBy=multi-user.target

6. Reload systemd to recognize the new service file:
  sudo systemctl daemon-reload

7. Start the service immediately:
  sudo systemctl start rover_monitor.service

8. Enable the service to start automatically on boot:
   sudo systemctl enable rover_monitor.service

9. sudo systemctl enable rover_monitor.service





