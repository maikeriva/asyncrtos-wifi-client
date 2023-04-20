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

## How do I use this?

Check out the examples folder.

## How do I contribute?

Feel free to contribute with code or a coffee :)

<a href="https://www.buymeacoffee.com/micriv" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 60px !important;width: 217px !important;" ></a>
