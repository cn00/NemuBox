/* $Id: Hello.d $ */
/** @file
 * NemuDTrace - Hello world sample.
 */


/* This works by matching the dtrace:::BEGIN probe, printing the greeting and
   then quitting immediately. */
BEGIN {
    trace("Hello Nemu World!");
    exit(0);
}

