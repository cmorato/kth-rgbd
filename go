cd ~
sudo rm -f data_out/*.bmp
sudo rm -f data_gen/*
sudo ~/Projects/kth-rgbd/bin/rgbd_slam --save $1
cd --
