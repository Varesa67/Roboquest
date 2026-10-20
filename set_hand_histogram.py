import cv2
import numpy as np
import pickle

def build_squares(img):
	x, y = 390, 75
	cell_size, gap = 22, 8
	rows = []
	for row in range(10):
		cells = []
		for col in range(5):
			cell_x = x + col * (cell_size + gap)
			cell_y = y + row * (cell_size + gap)
			cells.append(img[cell_y:cell_y+cell_size, cell_x:cell_x+cell_size])
			cv2.rectangle(
				img,
				(cell_x, cell_y),
				(cell_x + cell_size, cell_y + cell_size),
				(0, 255, 0),
				2,
			)
		rows.append(np.hstack(cells))
	return np.vstack(rows)

def get_hand_hist():
	cam = cv2.VideoCapture(0)
	if not cam.isOpened():
		cam.release()
		cam = cv2.VideoCapture(1)
	if not cam.isOpened():
		raise RuntimeError("Could not open a webcam. Check that it is connected and not in use.")
	x, y, w, h = 300, 100, 300, 300
	flagPressedC, flagPressedS = False, False
	imgCrop = None
	hist = None
	try:
		while True:
			ok, img = cam.read()
			if not ok:
				raise RuntimeError("Could not read a frame from the webcam.")
			img = cv2.flip(img, 1)
			img = cv2.resize(img, (640, 480))
			hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)

			keypress = cv2.waitKey(1) & 0xFF
			if keypress == ord('c'):
				if imgCrop is None:
					print("Wait for the sample grid to appear, then press C.")
					continue
				hsvCrop = cv2.cvtColor(imgCrop, cv2.COLOR_BGR2HSV)
				flagPressedC = True
				hist = cv2.calcHist([hsvCrop], [0, 1], None, [180, 256], [0, 180, 0, 256])
				cv2.normalize(hist, hist, 0, 255, cv2.NORM_MINMAX)
			elif keypress == ord('s'):
				if hist is None:
					print("Press C while your hand is in the sample grid before saving.")
					continue
				flagPressedS = True
				break
			if flagPressedC:
				dst = cv2.calcBackProject([hsv], [0, 1], hist, [0, 180, 0, 256], 1)
				disc = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (10, 10))
				cv2.filter2D(dst, -1, disc, dst)
				blur = cv2.GaussianBlur(dst, (11, 11), 0)
				blur = cv2.medianBlur(blur, 15)
				_, thresh = cv2.threshold(
					blur, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU
				)
				cv2.imshow("Thresh", cv2.merge((thresh, thresh, thresh)))
			if not flagPressedS:
				imgCrop = build_squares(img)
			cv2.putText(
				img,
				"Cover green squares with your palm. C = sample, S = save",
				(15, 35),
				cv2.FONT_HERSHEY_SIMPLEX,
				0.55,
				(0, 255, 0),
				2,
			)
			cv2.imshow("Set hand histogram", img)
	finally:
		cam.release()
		cv2.destroyAllWindows()

	with open("hist", "wb") as f:
		pickle.dump(hist, f)


if __name__ == "__main__":
	get_hand_hist()
