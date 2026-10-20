import os
import pickle
import random
import sqlite3

import cv2
import numpy as np

image_x, image_y = 50, 50

def get_hand_hist():
	with open("hist", "rb") as f:
		hist = pickle.load(f)
	return hist

def init_create_folder_database():
	os.makedirs("gestures", exist_ok=True)
	with sqlite3.connect("gesture_db.db") as conn:
		conn.execute(
			"CREATE TABLE IF NOT EXISTS gesture "
			"(g_id INTEGER NOT NULL PRIMARY KEY, g_name TEXT NOT NULL)"
		)

def create_folder(folder_name):
	if not os.path.exists(folder_name):
		os.mkdir(folder_name)

def store_in_db(g_id, g_name):
	with sqlite3.connect("gesture_db.db") as conn:
		existing = conn.execute(
			"SELECT 1 FROM gesture WHERE g_id = ?", (g_id,)
		).fetchone()
		if existing:
			choice = input("That ID already exists. Replace its name? (y/n): ")
			if choice.lower() != "y":
				raise ValueError("Choose a different gesture ID and run this script again.")
			conn.execute("UPDATE gesture SET g_name = ? WHERE g_id = ?", (g_name, g_id))
		else:
			conn.execute(
				"INSERT INTO gesture (g_id, g_name) VALUES (?, ?)", (g_id, g_name)
			)
	
def store_images(g_id):
	total_pics = 200
	hist = get_hand_hist()
	cam = cv2.VideoCapture(0)
	if not cam.isOpened():
		cam.release()
		cam = cv2.VideoCapture(1)
	if not cam.isOpened():
		raise RuntimeError("Could not open a webcam. Check that it is connected and not in use.")
	x, y, w, h = 300, 100, 300, 300

	create_folder(os.path.join("gestures", str(g_id)))
	pic_no = 0
	flag_start_capturing = False
	frames = 0
	try:
		while True:
			ok, img = cam.read()
			if not ok:
				raise RuntimeError("Could not read a frame from the webcam.")
			img = cv2.flip(img, 1)
			img_hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
			dst = cv2.calcBackProject([img_hsv], [0, 1], hist, [0, 180, 0, 256], 1)
			disc = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (10, 10))
			cv2.filter2D(dst, -1, disc, dst)
			blur = cv2.GaussianBlur(dst, (11, 11), 0)
			blur = cv2.medianBlur(blur, 15)
			thresh = cv2.threshold(blur, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)[1]
			thresh = cv2.cvtColor(cv2.merge((thresh, thresh, thresh)), cv2.COLOR_BGR2GRAY)
			thresh = thresh[y:y+h, x:x+w]
			contours = cv2.findContours(
				thresh.copy(), cv2.RETR_TREE, cv2.CHAIN_APPROX_NONE
			)[0]

			if contours:
				contour = max(contours, key=cv2.contourArea)
				if cv2.contourArea(contour) > 10000 and frames > 50 and flag_start_capturing:
					x1, y1, w1, h1 = cv2.boundingRect(contour)
					save_img = thresh[y1:y1+h1, x1:x1+w1]
					if w1 > h1:
						pad = (w1 - h1) // 2
						save_img = cv2.copyMakeBorder(
							save_img, pad, w1 - h1 - pad, 0, 0,
							cv2.BORDER_CONSTANT, (0, 0, 0)
						)
					elif h1 > w1:
						pad = (h1 - w1) // 2
						save_img = cv2.copyMakeBorder(
							save_img, 0, 0, pad, h1 - w1 - pad,
							cv2.BORDER_CONSTANT, (0, 0, 0)
						)
					save_img = cv2.resize(save_img, (image_x, image_y))
					if random.randint(0, 1):
						save_img = cv2.flip(save_img, 1)
					pic_no += 1
					image_path = os.path.join(
						"gestures", str(g_id), f"{pic_no}.jpg"
					)
					if not cv2.imwrite(image_path, save_img):
						raise OSError(f"Could not save captured image: {image_path}")
					cv2.putText(img, "Capturing...", (30, 60),
						cv2.FONT_HERSHEY_TRIPLEX, 2, (127, 255, 255))

			cv2.rectangle(img, (x, y), (x+w, y+h), (0, 255, 0), 2)
			cv2.putText(img, f"{pic_no}/{total_pics}", (30, 400),
				cv2.FONT_HERSHEY_TRIPLEX, 1.5, (127, 127, 255))
			cv2.imshow("Capturing gesture", img)
			cv2.imshow("thresh", thresh)
			keypress = cv2.waitKey(1) & 0xFF
			if keypress == ord("q") or pic_no >= total_pics:
				break
			if keypress == ord("c"):
				flag_start_capturing = not flag_start_capturing
				frames = 0
			if flag_start_capturing:
				frames += 1
	finally:
		cam.release()
		cv2.destroyAllWindows()
	print(f"Captured {pic_no} images for gesture {g_id}.")

if __name__ == "__main__":
	init_create_folder_database()
	while True:
		try:
			g_id = int(input("Type only the gesture ID number (for example, 0): "))
			if g_id < 0:
				print("Gesture ID cannot be negative. Enter 0 or a higher number.")
				continue
			break
		except ValueError:
			print("Please type only a number, such as 0, without words or punctuation.")
	g_name = input("Enter gesture name/text: ").strip()
	if not g_name:
		raise ValueError("Gesture name cannot be empty.")
	store_in_db(g_id, g_name)
	store_images(g_id)