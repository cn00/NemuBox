# sed class script to modify /etc/devlink.tab
!install
/name=nemuguest/d
$i\
type=ddi_pseudo;name=nemuguest  \\D

!remove
/name=nemuguest/d

