# Architecture

## Integration strategy

Use the documented Microsoft Snipping Tool protocol (`ms-screenclip://`)
from an MSIX-packaged application.

The application will:

1. Launch Snipping Tool using the packaged application's URI-launch API.
2. Register and receive its own protocol callback.
3. Validate the callback status and request correlation ID.
4. Redeem the returned file-access token.
5. Process the resulting screenshot.

## Scrolling capture pipeline

The scrolling engine will separately manage:

1. Target window or capture region selection.
2. Repeated viewport capture.
3. Controlled scrolling.
4. Frame overlap detection.
5. Image alignment and stitching.
6. Export.

## Design principle

Keep Snipping Tool integration, screen capture, scrolling,
image processing and application UI as independent modules.