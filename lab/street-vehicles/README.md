# Photographic vehicle observations

Run the open NanoDet detector on an atlas street-sample manifest with its credited photographs present locally. Use Python with NumPy and OpenCV. The script verifies the float model’s SHA-256 and groups ONNX outputs by resolution and tensor shape, including OpenCV 5's ordering. All inference runs locally. [Pinned model and Apache license](https://huggingface.co/opencv/object_detection_nanodet/tree/5bfd47077350a726ad440dd7bd1e1e35e8ebcfb2) · [OpenCV Zoo reference](https://github.com/opencv/opencv_zoo/tree/main/models/object_detection_nanodet)

```sh
curl -fL https://huggingface.co/opencv/object_detection_nanodet/resolve/5bfd47077350a726ad440dd7bd1e1e35e8ebcfb2/object_detection_nanodet_2022nov.onnx -o nanodet.onnx
python lab/street-vehicles/detect.py nanodet.onnx samples.json observations.json
```

The I Street sequence from July 24, 2022 contains 12, 9 and 4 detected vehicle boxes in its three consecutive images. Its GPS fixes report 4.3–4.8 m uncertainty. Repeated images show a black pickup beside the east curb. The captured vehicles have unknown present-day positions. We retained these boxes for review and kept the retail driving corridor clear of detector-generated obstacles. The image credit and source links are in [the manifest](manifest.json).

A second local trial executed the 3.8 MB network using ONNX Runtime Web 1.31.1 SIMD WebAssembly, one CPU thread and Bun. Processing the 24 decoded photos from eight districts took 37–74 ms per frame. Masking likely road users changed the estimated pavement color or number of accepted patches in three frames. The largest per-channel color change was three 8-bit levels. The game does not distribute these model/runtime binaries. [Individual results](study.json) · [ONNX Runtime Web](https://onnxruntime.ai/docs/get-started/with-javascript/web.html)

We also tested the already-downloaded public laser-survey samples at eight earlier sites. Only one car-sized object was independently visible in two flight strips at similar dimensions and position. The samples cover limited portions of a street. Anchorage’s I Street crosses an Entwine subtree boundary within the roadway. Adding the adjoining subtree retrieved 4,509 bare-earth returns from two flights across the next 89 metres. The two flights resolve a height change near −5.75 m from the mapped axis. A separate red-curb measurement in the street photographs disagrees with interpreting that ledge as the west curb. This prevented an unsupported parking placement. [USGS 3DEP](https://www.usgs.gov/3d-elevation-program) · [Entwine Point Tile hierarchy](https://entwine.io/en/latest/entwine-point-tile.html)
