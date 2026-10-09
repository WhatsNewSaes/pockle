# Third-party components

The MIT license in `LICENSE` covers this project's own code, tools and documentation. These components keep their own terms:

- The Waveshare e-paper display driver (vendor/ and firmware/RetroSports/EPD_3in97.*,
  DEV_Config.*) and Espressif's ES8311 codec driver (firmware/RetroSports/es8311.*)
  carry their original notices.
- The QR code encoder (firmware/RetroSports/qrcodegen.c, qrcodegen.h) is Project Nayuki's
  QR Code generator library, MIT License (https://www.nayuki.io/page/qr-code-generator-library).
- The Inter typeface (assets/fonts/) is licensed under the SIL Open Font License 1.1
  (assets/fonts/OFL.txt).
- Bible texts loaded onto devices are the Berean Standard Bible (public domain), the
  Free Bible Version (CC BY-SA 4.0, Jonathan Gallagher) and the Bible in Basic English
  (public domain); they are fetched by tools/build_bible.py and are not stored in this
  repository.
- Team logos are trademarks of their respective owners, used for identification only.
- The devotionals in devotionals/ are licensed separately under CC BY 4.0
  (devotionals/LICENSE).
- The Bible pictures (scenes/out/*.img, one for every day's verse and every character card)
  are © Sweet Publishing, licensed CC BY-SA 3.0 (https://sweetpublishing.com, via Wikimedia
  Commons, "Bible illustrations by Sweet Media"), reduced to 1-bit for the e-paper panel by
  tools/build_scenes.py; each carries its credit line on the device. tools/build_scenes.py can
  also take public-domain engravings by Gustave Doré (1866) and Julius Schnorr von Carolsfeld (1860).
