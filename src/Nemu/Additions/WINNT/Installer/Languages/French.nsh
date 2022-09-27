; $Id$
; @file NLS for French language.
;

;
; Copyright (C) 2006-2014 Oracle Corporation
;
; This file is part of VirtualBox Open Source Edition (OSE), as
; available from http://www.virtualbox.org. This file is free software;
; you can redistribute it and/or modify it under the terms of the GNU
; General Public License (GPL) as published by the Free Software
; Foundation, in version 2 as it comes in the "COPYING" file of the
; VirtualBox OSE distribution. VirtualBox OSE is distributed in the
; hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
;

LangString NEMU_TEST ${LANG_FRENCH}                                 "Ceci est un message de test de $(^Name)!"

LangString NEMU_NOADMIN ${LANG_FRENCH}                              "Vous avez besoin de droits d'administrateur pour (dés)installer $(^Name)!$\r$\nCe programme d'installation se terminera maintenant."

LangString NEMU_NOTICE_ARCH_X86 ${LANG_FRENCH}                      "Cette application peut seulement être executée sur des systèmes Windows 32-bit. Veuillez installer la version 64-bit de $(^Name)!"
LangString NEMU_NOTICE_ARCH_AMD64 ${LANG_FRENCH}                    "Cette application peut seulement être executée sur des systèmes Windows 64-bit. Veuillez installer la version 32-bit de $(^Name)!"
LangString NEMU_NT4_NO_SP6 ${LANG_FRENCH}                           "Le programme d'installation a détécté que vous utilisez Windows NT4 sans Service Pack 6.$\r$\nNous vous conseillons d'installer ce Service Pack avant de continuer. Désirez vous cependant continuer?"

LangString NEMU_PLATFORM_UNSUPPORTED ${LANG_FRENCH}                 "Les Additions invité ne sont pas encore supportés sur cette plateforme!"

LangString NEMU_SUN_FOUND ${LANG_FRENCH}                            "Une ancienne version des Additions invité Sun est installée dans cette machine virtuelle. Les Additions invité actuelles ne peuvent être installées avant que cette version ne soit désinstallée.$\r$\n$\r$\nVoulez-vous désinstaller l'ancienne version maintenant?"
LangString NEMU_SUN_ABORTED ${LANG_FRENCH}                          "Le programme ne peut pas continuer avec l'installation des Additions invité.$\r$\nVeuillez désinstaller d'abord les anciennes Additions Sun!"

LangString NEMU_INNOTEK_FOUND ${LANG_FRENCH}                        "Une ancienne version des Additions invité est installée dans cette machine virtuelle. Les Additions invité actuelles ne peuvent être installées avant que cette version ne soit désinstallée.$\r$\n$\r$\nVoulez-vous désinstaller l'ancienne version maintenant?"
LangString NEMU_INNOTEK_ABORTED ${LANG_FRENCH}                      "Le programme ne peut pas continuer avec l'installation des Additions invité.$\r$\nVeuillez désinstaller d'abord les anciennes Additions!"

LangString NEMU_UNINSTALL_START ${LANG_FRENCH}                      "Choisissez OK pour démarrer la désinstallation.$\r$\nLe processus nécessitera quelque temps et se déroulera en arrière-plan."
LangString NEMU_UNINSTALL_REBOOT ${LANG_FRENCH}                     "Nous vous conseillons fortement de redémarer cette machine virtuelle avant d'installer la nouvelle version des Additions invité.$\r$\nVeuillez recommencer l'installation des Additions après le redémarrage.$\r$\n$\r$\nRedémarrer maintenant?"

LangString NEMU_COMPONENT_MAIN ${LANG_FRENCH}                       "Additions invité VirtualBox"
LangString NEMU_COMPONENT_MAIN_DESC ${LANG_FRENCH}                  "Fichiers prinipaux des Additions invité VirtualBox"
LangString NEMU_COMPONENT_AUTOLOGON ${LANG_FRENCH}                  "Support identification automatique"
LangString NEMU_COMPONENT_AUTOLOGON_DESC ${LANG_FRENCH}             "Active l'identification automatique dans l'invité"
LangString NEMU_COMPONENT_AUTOLOGON_WARN_3RDPARTY ${LANG_FRENCH}    "Un composant permettant l'identification automatique est déjà installé.$\r$\nSi vous le remplacé avec le composant issue de VirtualBox, cela pourrait déstabiliser le système.$\r$\nDésirez-vous cependant continuer?"
LangString NEMU_COMPONENT_D3D  ${LANG_FRENCH}                       "Support Direct3D pour invités (experimental)"
LangString NEMU_COMPONENT_D3D_DESC  ${LANG_FRENCH}                  "Active le support Direct3D pour invités (experimental)"
LangString NEMU_COMPONENT_D3D_NO_SM ${LANG_FRENCH}                  "Windows ne fonctionne pas en mode sans échec.$\r$\nDe ce fait, le support D3D ne peut être installé."
LangString NEMU_COMPONENT_D3D_NOT_SUPPORTED ${LANG_FRENCH}          "Le support invité pour Direct3D n'est pas disponible sur Windows $g_strWinVersion!"
LangString NEMU_COMPONENT_D3D_OR_WDDM ${LANG_FRENCH}                "Ce système supporte l'interface Windows Aero (WDDM).$\r$\nLe support VirtualBox pour cette fonctionalité est expérimental et ne devrait pas encore être utilisé sur des systèmes importants.$\r$\n$\r$\nVoulez-vous installer le support Direct3D de base à la place?"
LangString NEMU_COMPONENT_D3D_HINT_VRAM ${LANG_FRENCH}              "Veuillez noter que l'utilisation de l'accélération 3D nécécssite au moins 128 MB de mémoire vidéo ; pour un utilisation avec plusieurs écrans nous recommandons  d'affecter 256 MB.$\r$\n$\r$\nSi nécéssaire vous pouvez changer la taille du mémoire vidéo dans la sous-section $\"Affichage$\" des paramètres de la machine virtuelle."
LangString NEMU_COMPONENT_D3D_INVALID ${LANG_FRENCH}                "Le programme d'installation a détecté une installation DirectX corrompue ou invalide.$\r$\n$\r$\nAfin d'assurer le bon fonctionnement du support DirectX, nous conseillons de réinstaller le moteur d'exécution DirectX.$\r$\n$\r$\nDésirez-vous cependant continuer?"
LangString NEMU_COMPONENT_D3D_INVALID_MANUAL ${LANG_FRENCH}         "Voulez-vous voir le manuel d'utilisateur VirtualBox pour chercher une solution?"

LangString NEMU_WFP_WARN_REPLACE ${LANG_FRENCH}                     "Le programme d'installation vient de remplacer certains fichiers systèmes afin de faire fonctionner correctement ${PRODUCT_NAME}.$\r$\nPour le cas qu'un avertissement de la Protection de fichiers Windows apparaisse, veuiller l'annuler sans restaurer les fichiers originaux!"
LangString NEMU_REBOOT_REQUIRED ${LANG_FRENCH}                      "Le système doit être redémarré pourque les changements prennent effet. Redémarrer Windows maintenant?"

LangString NEMU_EXTRACTION_COMPLETE ${LANG_FRENCH}                  "$(^Name): Les fichiers ont été extrait avec succès dans $\"$INSTDIR$\"!"

LangString NEMU_ERROR_INST_FAILED ${LANG_FRENCH}                    "Une erreur est survenue pendant l'installation!$\r$\nVeuillez consulter le fichier log '$INSTDIR\install_ui.log' pour plus d'informations."
LangString NEMU_ERROR_OPEN_LINK ${LANG_FRENCH}                      "Impossible d'ouvrir le lien dans le navigateur par défaut."

LangString NEMU_UNINST_CONFIRM ${LANG_FRENCH}                       "Voulez-vous vraiment désinstaller $(^Name)?"
LangString NEMU_UNINST_SUCCESS ${LANG_FRENCH}                       "$(^Name) ont été désinstallés."
LangString NEMU_UNINST_INVALID_D3D ${LANG_FRENCH}                   "Installation incorrecte du support Direct3D detectée; une désinstallation ne sera pas tentée."
LangString NEMU_UNINST_UNABLE_TO_RESTORE_D3D ${LANG_FRENCH}         "La restauration des fichiers originaux Direct3D a echoué. Veuillez réinstaller DirectX."
