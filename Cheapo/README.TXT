
Now that QuakeWorld Source has been released making
all kinds of cheats easy, keeping the proxy source
closed is not that important. So I'm releasing the
Cheapo source under GNU General Public License
(see copying.txt in the package).

Despite these cheating problems, I think that open
sourcing Quake is a great thing. Now that cheating
problems are exposed, it is a good time to develop
a solution for authenticating clients across the
net. These developments can not only serve QW, but
other net games as well.

A primitive form of authentication consists of a
closed source loader, like Carmack mentioned in his
plan. Cheapo 2.6 and Qizmo (http://qizmo.sci.fi)
can perform this, although it is not perfect: users
have to manually compare the printed checksums on
a server. Qizmo also has additional cheat checks
like f_skins.

A better solution is to integrate the checks in the
server, so that they are performed automatically.
I hope an open source alternative will be found that
is as effective as the closed source one. Perfection
is impossible, because in principle a Cheat-QW could
keep (or even run in parallel) a nonmodified QW and
use it for any checks.

Differences to the binary version 2.6:

Code for f_modified is removed from the source so the
last official binary only Cheapo can still be used to
verify the proxy and QuakeWorld client. Unfortunately
this does not stop people from making silent versions
of the proxy, or versions that contain cheats.

A basic division of source:

MAIN.C  - socket communication and os specific stuff
CMD.C   - .command interface and help
PROXY.C - qw protocol parsing/modifications
          (actual proxy operations happen here)
MISC.C  - all kinds of utilities, many things like
          pointing are handled in routines here.

Compiling:

Version 2.61 has only been tested on Windows 95/98/NT
(use 'nmake') and Linux 2.2 (use script 'mklinux').

Older version were also compiled on Sparc and Alpha
architectures and this version probably works as
well, but hasn't been tested. Cheapo should compile
on just about anything as long as you can find
headers/libraries for the Unix sockets interface.

--
Sami Tammilehto

