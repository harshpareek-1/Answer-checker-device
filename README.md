# Answer-checker-device
A smart tool that gamifies study sessions and improves efficiency with real-time feedback.
---

## 🚀 Overview

This project is a hardware + web system designed to make solving multiple-choice questions faster and more engaging.

Instead of checking answers after completing a test, this system allows real-time answer validation while solving, improving focus and efficiency.

---

## ❗ Problem

During exam preparation like JEE, checking answers manually:

- breaks concentration
- slows down practice
- reduces efficiency

---

## 💡 Solution

This system allows users to:

1. Upload an image of the answer key
2. Convert it into structured answers using AI
3. Send answers to an ESP32 device
4. Get instant feedback while solving questions

---

## ⚙️ System Architecture
---

## 🔩 Hardware Components

- ESP32
- LCD Display (I2C)
- Push Buttons (A/B/C/D input)
- Buzzer
- LEDs

---

## 💻 Software Stack

- Embedded C (Arduino framework)
- Web interface for uploading answer key
- Claude API (image → text conversion)
- Google Sheets API (score logging)

---

## ✨ Features

- Real-time answer checking
- Audio feedback using buzzer
- Visual feedback using LEDs and display
- AI-based answer key extraction
- Automatic score logging

---

## 📸 Images

![IMG_1075](https://github.com/user-attachments/assets/56f7504c-33e3-4415-843a-8ccc83ae7bb7)
![IMG_1074](https://github.com/user-attachments/assets/58d1e819-1f2b-41cb-a0db-b9ef209e3e54)
![IMG_1072](https://github.com/user-attachments/assets/af36f47f-1207-4cc4-a93d-08eced1dbfd7)
![IMG_1073](https://github.com/user-attachments/assets/1ac91924-c1e4-458e-93cc-9d4aae730052)
![IMG_1071](https://github.com/user-attachments/assets/d4fae610-6223-4b3f-a9ba-0d9cbaa6b2ea)


---

## 🔮 Future Improvements

- Multi-user support
- Performance analytics dashboard
- Improved OCR accuracy
- Mobile app integration

---

## 🧠 Learnings

- Built a complete hardware + software pipeline
- Integrated APIs with embedded systems
- Designed a real-time feedback system
- Improved problem-solving efficiency through engineering
