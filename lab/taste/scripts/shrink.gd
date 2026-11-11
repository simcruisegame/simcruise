extends SceneTree
# godot --headless --script shrink.gd -- OUT_DIR WIDTH QUALITY file1.jpg file2.jpg ...
#   writes each file resized to WIDTH (keeping aspect) as OUT_DIR/<prefix><name>.jpg
# godot --headless --script shrink.gd -- --sheet OUT.jpg COLS CELL_W QUALITY file1 file2 ...
#   writes a contact sheet
func _initialize():
	var a := OS.get_cmdline_user_args()
	if a[0] == "--sheet":
		var out := a[1]; var cols := int(a[2]); var cw := int(a[3]); var q := float(a[4])
		var files := a.slice(5)
		var first := Image.load_from_file(files[0])
		var ch := int(round(first.get_height() * float(cw) / first.get_width()))
		var rows := int(ceil(files.size() / float(cols)))
		var sheet := Image.create(cols * cw, rows * ch, false, Image.FORMAT_RGB8)
		sheet.fill(Color(0.1, 0.1, 0.1))
		for i in files.size():
			var im := Image.load_from_file(files[i]); im.convert(Image.FORMAT_RGB8)
			im.resize(cw, ch, Image.INTERPOLATE_LANCZOS)
			sheet.blit_rect(im, Rect2i(0, 0, cw, ch), Vector2i((i % cols) * cw, (i / cols) * ch))
		sheet.save_jpg(out, q)
		print("sheet ", out)
	else:
		var out_dir := a[0]; var w := int(a[1]); var q := float(a[2])
		for f in a.slice(3):
			var im := Image.load_from_file(f); im.convert(Image.FORMAT_RGB8)
			var h := int(round(im.get_height() * float(w) / im.get_width()))
			im.resize(w, h, Image.INTERPOLATE_LANCZOS)
			var name := f.get_file().get_basename().trim_prefix("nb1999_")
			im.save_jpg(out_dir.path_join(name + ".jpg"), q)
			print("wrote ", name)
	quit()
