# AsyncRTOS wifi client

A wifi client implementation built on top of AsyncRTOS and ESP-IDF.

## Why?

The official ESP-IDF wifi API is good, but it is not always idempotent. When building apps on AsyncRTOS, I found it more convenient to abstract it away with a microservice which:

- Supports AsyncRTOS asyncronous message passing
- Has idempotent behavior
- Is thread-safe
- Fails gracefully
- Has keep-alive functionality (automatically reconnects in case of errors)
- Performs multiple connection attepts before giving up

Feel free to contribute, or buy me a coffee.

## How to use this

Here's a simple example. It provides no error checking for illustration purposes, always check futures are resolved with `aos_isresolved` and `out_err` values.
