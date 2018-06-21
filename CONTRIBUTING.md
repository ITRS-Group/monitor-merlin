
# Contributing

The developers at OP5 are very happy to accept and maintain patches, as long  
as the code change fits with the general concepts of the project.  
In general, we prefer patches to be in the form of pull requests on Github  
if the project is available there, otherwise you can email patches to OP5  
at [op5-users@lists.op5.com](op5-users@lists.op5.com).

Once you have submitted your pull request or email, the pull request will  
enter our internal verification process, where we might check the format of  
the code, the amount of testing needed, etc. After this process is done, you  
will be notified in the same place that you sent your patches.

## Hints when writing commits

  * Make commits of logical units
  * Check for unnecessary whitespace with "git diff --check" before committing
  * Do not check in commented out code or unneeded files
  * The first line of the commit message should be a short description and  
  should skip the full stop
  * One or more paragraphs, outlining the _what_ and the _why_ of the change.  
  That is; What changed? Why was the change necessary?
  * if you want your work included upstream, add a "Signed-off-by: Your Name  
  <you@example.com>" line to the commit message (or just use the option "-s"  
  when committing) to confirm that you agree to the [Developer's Certificate of Origin](https://developercertificate.org/)
  * Make sure that you have tests for the bug you are fixing if possible
  * Make sure that the test suite passes after your commits
  * Provide a meaningful commit message

A good example message fixing some specific part of the codebase:

```
commit a5f0f04e34ab9d5851d21575f97cbb7430c9a9af
Author: Robin Engström <robin.engstrom@op5.com>
Date:   Wed May 17 13:21:19 2017 +0200

    hooks.c: don't block passive check results from being propagated

    Previously we assumed that if we had ended up in the NEBTYPE_SERVICECHECK_PROCESSED
    as a result of receiving a merlin packet from another node that the packet would
    contain a check result. And since we don't want check results to start bouncing
    between nodes we would block it.

    However, there is another way that we could end up in NEBTYPE_SERVICECHECK_PROCESSED
    as a result from received packet. That is if a fellow node has sent us an external
    command containing a passive check result for an object which turns out to be our
    responsibility. In this case we still want to send the check result to our peers and
    masters.

    Part of MON-10221
    Signed-off-by: Robin Engström <robin.engstrom@op5.com>
```

A (very) bad commit message looks like this:
```
build fix
it broke on solaris
```
The latter is a horrible message, because it doesn't tell us which part of  
the build broke, or why, or how the fixer came to the conclusion that the  
implemented fix was the best one, or what to look out for in the future.

## Pull requests

A pull request should preferably contain tests, we don't merge anything that  
isn't tested. If tests aren't included or the pull request causes build fails  
we will create internal tickets to troubleshoot and/or add tests and take it  
into consideration in the upcoming sprint review(s).


