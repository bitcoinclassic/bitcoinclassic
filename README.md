Bitcoin Core - Figure It Out 
=====================================

[![Build Status](https://travis-ci.org/bitcoin/bitcoin.svg?branch=bitcoin-fio)](https://travis-ci.org/bitcoin/bitcoin)

https://www.bitcoin.org

What is Bitcoin?
----------------

Bitcoin is an experimental new digital currency that enables instant payments to
anyone, anywhere in the world. Bitcoin uses peer-to-peer technology to operate
with no central authority: managing transactions and issuing money are carried
out collectively by the network. Bitcoin Core is the name of open source
software which enables the use of this currency.

For more information, as well as an immediately useable, binary version of
the Bitcoin Core software, see https://www.bitcoin.org/en/download.

What is Bitcoin - Figure It Out?
--------------------------------

Bitcoin - Figure It out is an aggressive fork of the bitcoin network designed to increase the blocksize limits in a very conservative way. Recently there has been several disagreements about which BIP to implement in regards to blocksize. BIP 101 vs. BIP 100. Both of these BIPs introduce larger blocksize limits but require consensus of the network. Getting consensus is very difficult on the bitcoin network. Pools control consensus and the idea is that miners will flock to whichever pool they think is implementing the best solution. I think that this ideology is flawed. I do not have a better idea, and I think that this idea is currently the best we have. Bitcoin-fio is an alternate way to gain consensus. Bitcoin-fio is a bitcoin core - based client which starts accepting 2mb block immediately. The idea is to give us more time to decide how blocksize-increase should be handled. A majority of core dev and the community agree that we need to increase blocksize, the disagreement is over how much to increase and when. I have attempted to extend the duration of time we have available to make these important decisions. This is not a permanent solution by any means. I believe a majority of the bitcoin ecosystem believes that increasing the blocksize by a very small amount would be beneficial to bitcoin as a whole and would give us more time to sort things out. I have based this client on that belief.

License
-------

Bitcoin Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see http://opensource.org/licenses/MIT.

Development process
-------------------

Developers work in their own trees, then submit pull requests when they think
their feature or bug fix is ready.

If it is a simple/trivial/non-controversial change, then one of the Bitcoin
development team members simply pulls it.

If it is a *more complicated or potentially controversial* change, then the patch
submitter will be asked to start a discussion (if they haven't already) on the
[mailing list](https://lists.linuxfoundation.org/mailman/listinfo/bitcoin-dev)

The patch will be accepted if there is broad consensus that it is a good thing.
Developers should expect to rework and resubmit patches if the code doesn't
match the project's coding conventions (see [doc/developer-notes.md](doc/developer-notes.md)) or are
controversial.

The `master` branch is regularly built and tested, but is not guaranteed to be
completely stable. [Tags](https://github.com/bitcoin/bitcoin/tags) are created
regularly to indicate new official, stable release versions of Bitcoin.

Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

### Automated Testing

Developers are strongly encouraged to write unit tests for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run (assuming they weren't disabled in configure) with: `make check`

Every pull request is built for both Windows and Linux on a dedicated server,
and unit and sanity tests are automatically run. The binaries produced may be
used for manual QA testing — a link to them will appear in a comment on the
pull request posted by [BitcoinPullTester](https://github.com/BitcoinPullTester). See https://github.com/TheBlueMatt/test-scripts
for the build/test scripts.

### Manual Quality Assurance (QA) Testing

Large changes should have a test plan, and should be tested by somebody other
than the developer who wrote the code.
See https://github.com/bitcoin/QA/ for how to create a test plan.

Translations
------------

Changes to translations as well as new translations can be submitted to
[Bitcoin Core's Transifex page](https://www.transifex.com/projects/p/bitcoin/).

Translations are periodically pulled from Transifex and merged into the git repository. See the
[translation process](doc/translation_process.md) for details on how this works.

**Important**: We do not accept translation changes as GitHub pull requests because the next
pull from Transifex would automatically overwrite them again.

Translators should also subscribe to the [mailing list](https://groups.google.com/forum/#!forum/bitcoin-translators).
