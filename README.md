# iox
flow-based language


The interpreter included is a (very) rough draft just to try out the syntax.
It may be rewritten or improved if I like it enough ;)

Not all features are implemented. This document is just a plan of how everything will function and behave!

## Purpose

Create a pipe-centric shell alternative to play with async and reactive concepts (futures, coroutines, signal wakeups, etc.).

## Basics

### Input/Output

```
'hello, world' out
```

The above takes a string "hello, world" and pipes it to the function "out"
Note there is no "|" for piping like in bash.  It is implied between each token.

iox code reads from left-to-right.

```
0 $x
```

This pipes a value (0) into $x.

To read a value, you pipe it into something else, in this case, we pipe it to *out*, which outputs it:

```
$x out
```

To get line-based input, use "in".

```
"Enter your name: " in $name
```

The message "Enter your name: " is passed to *in*, which is displayed as a prompt.
*in* will pipe the string it receives from the user on to the next token.
In this case, it is stored in $name.

### Variables

By piping from a value into a named variable, we created a variable of that type
Variable types (*int*, etc.) are both constructors (pipe from) and casters (pipe to and from).

We can cast values as we're storing them.  In this case, it is redundant, since
0 is already an interger.
    
```
0 int $x
```

This pipes 0 into $x, ensuring it is stored as an int.

This is similar to x = int(0) in other languages.

Now, Let's write a basic program addition calculator:

First let's get two numbers from the user:

```
'Number: ' in int $num1
'Number: ' in int $num2
```

Now let's sum them and print:

```
$num1,$num2 + out
```

Notice the comma.  Commas are used to batch multiple things to send to a pipe.
The *+* function sums all the parameters together, and returns this number

### Branching

(I haven't fully implemented this feature, so this section of the documentation will serve only as example.)

First let's make a boolean called test, and branch based on its value.

Conditions are done with "?" representing the initial test,
and the code to run in either scenario

```
'Enter a boolean (true/false): ' in bool $test

$test ?
    'test is true!' out
else
    'test is false!' out
```

The *?* symbol is used for branching based on the contents of a stream.
The first branch is taken if the stream contains the boolean equivalent of *true*.
The else clause follows.

Because of the pipe-like order of tokens,
function parameters are written in postfix notation, meaning, we supply the
parameters first, separated by commas, then we call the function.

```
1,2,3 + out
```

This takes the 3 numbers, calls "+", which adds them all, then pipes that to out, which prints them. :)

Let's write a program that can greet a person by name:

```
'Enter your name: ' in $name
'Hello, ', $name out
```
### Packing/Unpacking

iox is based around temporary variables being passed down "the stream".  Generally these are single values or a list of values.

Variables are composite, meaning multiple variables can act as a single variable and our unpacked consecutively.
For example,

```
1,2,3 $numbers
0, $numbers, 4
```

### Scope

Indentations can push/pop scope.

```
1,2
    + $sum
    - $diff
    (_),(1,2) == assert
```

Notice that 1,2 remains in the stream for each line in the indent.

### Functions

Functions in iox take any number of inputs and give any number of outputs.

Here is a basic function declaration:

```
message:
    "Hello there, ", _

# Usage:
"What's your name? " in message out
```

Notice the *_* symbol represents the incoming data (unpacked) when piped from

The function automatically returns the content of the pipe on the last
effective line of the function.
We can block this behavior with the *;* symbol at the end of the line.

### Coroutines

The below features have no not yet been implemented.

The *&* symbol represents an async call, and you can tell a section of code to run in the background
The *&* symbol tells us to start any following code as a coroutine.

Let's have two threads sleep, then print something

```
& 2 sleep "2 second passed" out
& 1 sleep "1 second passed" out
```

The output will be 

```
1 second passed
2 second passed
```

All threads must finish for a program to complete, or a program must call quit for a program to
finish.

Contexts are named, and thus can be numbered. and you can sequence many operations on the same thread.

```
0 & "do this first" out
0 & "do this second" out
```

Since we need a handle to access data that becomes available after an async call,

```
alarm: & 5 sleep "I just slept!"

alarm out # wake-up on event (availability of future 'alarm')
```

### What now?

As noted before, not all the above features are implemented.  And there are definitely bugs.
More to come as I tinker.

