# Modèle mécanique, acoustique et sound design — ce qui est calculé, approché ou artistique

Chaque paramètre de l'interface porte un badge :

* **M — Mécanique** : entre dans le modèle mécanique simplifié (géométrie, temps, couples). Exact pour ce qu'il couvre.
* **A — Approximation** : paramètre d'un modèle physique simplifié (guides d'onde, filtres, lois empiriques).
* **S — Sound design** : contrôle purement artistique, aucune prétention physique.

Ce logiciel **ne fait pas** de CFD, de dynamique des gaz 1D complète, d'éléments finis électromagnétiques ou
structurels. Les modèles ci-dessous sont volontairement simples, cohérents et contrôlables.

## 1. Modélisation mécanique simplifiée (calculée)

### 1.1 Géométrie et événements de combustion — exacts
* Cylindrée = N · π/4 · alésage² · course.
* Chaque cylindre a un axe (en ligne 0°, V : 0° / angle entre bancs, boxer 0° / 180°, étoile k·360/N) et un angle de maneton.
  Il est au PMH quand *angle vilebrequin = axe − maneton (mod 360)*.
* 4 temps : un allumage par cycle de 720° sur l'un de ses deux PMH ; 2 temps et Wankel : un par tour (360°).
* Les manetons sont **dérivés** de l'ordre d'allumage de référence et de ses intervalles à l'angle de V nominal.
  Les tests vérifient que l'on retrouve les vilebrequins réels à manetons partagés (V8 cross-plane, V8 flat-plane,
  V12 60°, V10 90°, V-twin).
* Changer l'**angle entre bancs** recalcule les vrais intervalles (ex. V-twin maneton commun : 270/450 à 90°,
  315/405 à 45° ; V8 cross-plane dans un bloc à 60° : allumage irrégulier).
* Un ordre d'allumage saisi est **validé géométriquement** : chaque cylindre doit pouvoir s'allumer une fois dans le cycle
  au plus tôt possible ; sinon il est refusé avec explication. Les ordres possibles (choix de calage d'arbre à cames)
  sont énumérés (2^(N−1) combinaisons) et triés du plus régulier au moins régulier.
* Fréquence d'allumage = tr/s × événements par tour ; ordre moteur = événements par tour (2 pour un 4 cylindres 4 temps).

### 1.2 Dynamique du vilebrequin — modèle "valeur moyenne"
```
J dω/dt = T_ind − T_frott − T_pomp − T_charge (+ T_démarreur)
T_ind   = remplissage × facteur_suralim × (C_PO(N) + T_frott(N))     ⇒ en régime stabilisé pleine charge, couple = courbe
T_frott = FMEP(N) · Vd / (2π n_R),  FMEP = 0,97 + 0,15 N + 0,05 N² bar (N en milliers de tr/min, loi de type Barnes-Moss)
T_pomp  = 0,9 bar · (1 − remplissage) · Vd / (2π n_R)   (0,2 bar en diesel, pas de papillon)
```
* `n_R` = 2 en 4 temps, 1 en 2 temps / Wankel.
* Inertie J estimée si non saisie : 0,02 + 0,07 · L^0,9 kg·m² (ordre de grandeur vilebrequin + volant).
* Remplissage : cartographie pédale → débit d'air (courbe éditable), puis retard du 1er ordre (remplissage du collecteur).
* Ralenti : régulateur PI (comme une vanne de ralenti) avec anticipation calculée sur l'équilibre des couples au ralenti.
* Limiteur : coupure au-dessus du seuil, reprise sous seuil − hystérésis (types : carburant, allumage → pétards, coupure
  alternée). Coupure en décélération (DFCO) avec hystérésis.
* Démarreur : couple décroissant jusqu'à ~420 tr/min ; la combustion démarre au-dessus de 150 tr/min.
* Mode banc (dyno) : régime imposé (rampe), remplissage imposé ; le banc absorbe la différence de couple.
* La courbe de couple est le **couple effectif pleine charge** saisi par l'utilisateur. L'estimation automatique
  (PME ≈ 12 bar atmo essence, ×(1+pression)×0,82 suralimenté, 9 bar diesel) n'est qu'un point de départ (A).

### 1.3 Suralimentation (approximation)
* Turbo : vitesse d'arbre normalisée, cible = (N / N_pleine_pression) × (0,12 + 0,88 × remplissage), plafonnée par la
  wastegate ; 1er ordre avec constante de temps × (0,6 + 0,8 × taille). Rapport de pression = 1 + p_max × arbre².
  Le couple disponible est corrigé par (rapport actuel / rapport stabilisé) : le retard du turbo est donc audible
  **et** ressenti par la dynamique.
* Compresseur volumétrique : pression fonction du régime, sans retard ; centrifuge ∝ régime².
* Événement de décharge : fermeture rapide du papillon avec pression construite.
* Températures (gaz d'échappement, liquide) : premiers ordres **illustratifs** ; la température d'échappement
  change la vitesse du son dans les tubes (donc les résonances).

## 2. Modélisation acoustique (approximations)

### 2.1 Impulsions de combustion
* Pour chaque événement, une impulsion de pression d'échappement (ouverture de la soupape à
  EVO = 180 − 0,6 × (durée − 180) °, largeur 0,6 × durée, forme x^a (1−x)^b) définie **en degrés vilebrequin** :
  elle se raccourcit dans le temps quand le régime monte (son plus brillant), sans transposition artificielle.
* `a` diminue avec la dureté de combustion et le rapport volumétrique (front de pression plus raide) ; Wankel et diesel
  plus raides.
* Amplitude = (0,25 + 0,75 × remplissage^0,8) × écart statique du cylindre × variation cycle à cycle (plus forte à faible
  charge et avec un gros croisement de soupapes) ; ratés partiels possibles au ralenti avec un arbre agressif.
* Différences de longueur des primaires : retard par cylindre (grondement de boxer).
* Ondulation de vitesse du vilebrequin au rythme des combustions (modulation de phase, plus forte au ralenti).

### 2.2 Échappement — guides d'onde 1D
* Chaque tube : `y[n] = x[n] + R · LP(y[n − D])`, D = 2L/c·fs (aller-retour), R < 0 (extrémité ouverte), filtre de
  boucle à ka ≈ 1 (les courtes longueurs d'onde rayonnent au lieu de se réfléchir). Résonances aux harmoniques
  impairs de c/4L. c = √(γRT) avec la température des gaz (refroidissement le long de la ligne).
* Chaîne : collecteur/primaires → tube intermédiaire → turbine (amortissement selon la taille) → catalyseur →
  résonateur quart d'onde (encoche à c/4L et 3c/4L) → silencieux → tube de sortie → rayonnement.
* Silencieux à chambres : perte par transmission d'une chambre d'expansion
  TL = 10 log10(1 + ¼(m − 1/m)² sin² kL) → atténuations aux multiples impairs de c/4L_ch, m = rapport des sections ;
  bosse de Helmholtz basse fréquence (bourdonnement). Silencieux droit : absorption (passe-bas, étagère aiguë).
* Rayonnement : passe-haut 1er ordre à 130 Hz (pente ∝ f en dessous comme un monopôle, plat au-dessus) puis passe-bas
  à ka ≈ 1,5. (Un dérivateur pur rendait le ralenti 25–30 dB plus faible que la pleine charge.)
* Routage : simple, double (chaque banc séparé — c'est ce qui fait le "glouglou" du V8 cross-plane), X (35 % de
  mélange), H (18 %). Pour un moteur en ligne en double ligne : moitié avant / moitié arrière.

### 2.3 Admission
* Impulsions d'aspiration (négatives) à l'ouverture d'admission → tube de pipe d'admission → résonance de Helmholtz de
  la boîte à air (f = c/2π · √(A / (V·L_eff))) et du plenum → filtre selon le type (boîte d'origine, panneau, cône,
  cornets) → résonateur. Le papillon fermé bloque les impulsions et produit un souffle ∝ débit / ouverture.
* Reflux (croisement) : petite impulsion positive à faible charge.

### 2.4 Bruit mécanique, combustion rayonnée, vibrations
* Cliquetis du bloc à chaque combustion → 3 modes résonants (fréquences ∝ 1/taille du cylindre) ; diesel plus fort.
* Distribution : impulsions aux ouvertures/fermetures → modes de culbuterie ; chaîne (ordre 19–21) ; alternateur ;
  démarreur (rapport ~11).
* Vibrations : ordres 1 et 2 pondérés par une **table d'équilibrage** (forces primaires/secondaires, couples) — A.

### 2.5 Suralimentation (acoustique)
* La vraie fréquence de passage des aubes d'un turbo est ultrasonore : le sifflement audible est une **correspondance
  sonore** (S) de la vitesse d'arbre vers [f_min, f_max]. Bruit de rotation, décharge (atmosphérique / recirculée),
  pompage du compresseur sans décharge ("stututu"), souffle de wastegate externe.
* Compresseur Roots / vis : sifflement à rapport × lobes × régime (fréquence mécanique réelle).

### 2.6 Étalonnage
Les constantes `k*Level` de `ThermalVoice.cpp` équilibrent les couches à 0 dB de gain : mesurées avec
`esd_render --calibrate` (toutes les démos à 0,45 × zone rouge, charge 0,7 : mixage entre −21 et −25 dBFS RMS).

## 3. Sound design (libre)
Gains, panoramiques, solo/mute, largeur stéréo, gain maître, sifflement du turbo, probabilité de pétarades.
La phase 3 ajoutera automatisations multi-variables, effets, pistes de samples.

## 4. Ce qui est vérifié automatiquement (tests)
* Ordres d'allumage et intervalles de toutes les architectures ; rejet des ordres impossibles ; manetons partagés.
* Couple stabilisé pleine charge = courbe saisie ; ralenti tenu ; limiteur ; démarrage/arrêt ; retard du turbo.
* Rendu déterministe, sans NaN ni dépassement ; ordre 2 dominant pour un 4 cylindres ; demi-ordres (2,5 et 3,5)
  >20× plus forts sur une ligne de V8 cross-plane que de V8 flat-plane ; niveau croissant avec la charge ;
  mute effectif ; 96 kHz.

## 5. Moteurs électriques (phase 4)
* **Calculé (M)** : enveloppe de couple — couple constant jusqu'à la vitesse de base N_b = P_max / C_max, puis puissance
  constante (défluxage), nulle à la vitesse maximale ; fréquence électrique f_e = tr/s × paires de pôles (induction :
  / (1 − glissement), glissement = glissement nominal × couple relatif) ; courant = max(|C| / k_t, |P| / (0,95 U)),
  k_t = C_max / I_max ; boucle de courant du 1er ordre (5 ms) ; régénération au lever de pied et au freinage.
* **Approché (A)** : échauffement par pertes cuivre 3 I² R + pertes fer ∝ vitesse, capacité thermique globale.
* **Acoustique, fréquences exactes, amplitudes approchées** :
  forces radiales aux harmoniques 2k·f_e (fortes à 6k·f_e en commutation six-step / BLDC) ; passage d'encoches
  (encoches × tr/s, bandes latérales ± 2 f_e) ou de barres rotoriques (induction) ; cogging à ppcm(encoches, pôles) ;
  ondulation de couple 2·pp ; roulements (0,4 / 0,6 × billes × tr/s) ; onduleur : porteuse f_PWM et bandes
  f_PWM ± 2 f_e, 2 f_PWM ± f_e (PWM aléatoire → bande de bruit), claquements de commutation en six-step ;
  refroidissement (ventilateur dépendant de la température, ou pompe) ; deux modes de carter excités par les forces.
* Ce n'est pas un modèle électromagnétique par éléments finis.

## 6. Transmission et véhicule (phase 5)
* Embrayage fermé : une seule inertie J_source + m r² / R², force de route ramenée F r / (R η).
* Embrayage glissant : couple transmis = capacité × engagement × signe(glissement), embrayage automatisé au démarrage.
* Convertisseur : couple pompe (ω_e / K)² (1 − SR²), K = ω_calage / √C_max ; rapport de couple 2 → 1 jusqu'à SR = 0,85 ;
  lock-up à partir d'un rapport.
* Passages : manuelle / AMT = coupure de couple + ouverture d'embrayage ; DCT = passage sous couple (60 %), < 120 ms ;
  automatique = réduction de couple 30 %. Logique automatique : montée à un régime fonction de l'accélérateur, descente,
  rétrogradage forcé (kickdown).
* CVT : rapport choisi pour tenir un régime cible fonction de l'accélérateur, vitesse de variation limitée.
* Route : m g (C_rr cos α + sin α) + ½ ρ SCx v² + freinage. Pas de pneumatiques ni de dynamique latérale.

## 7. Hybride (phase 6)
* Série / série-parallèle : roues entraînées par le moteur électrique ; le thermique tourne en générateur à un régime fixé
  par la puissance demandée (+ recharge si SOC bas).
* Parallèle / full / plug-in / mild (P2) : thermique et électrique sur l'arbre d'entrée de boîte quand l'embrayage K0
  est fermé ; assistance, recharge par le moteur électrique, mode EV sous les seuils (vitesse, puissance), stop-start.
* Batterie : SOC intégré à partir de la puissance électrique (rendements 0,9), régénération limitée et coupée à SOC max.

## 8. Générateur de boucles (phase 7)
* Chaque boucle contient un **nombre entier de cycles moteur** : le régime de rendu est ajusté (< 1 %) pour que
  la longueur en échantillons corresponde exactement ; l'enregistrement démarre sur un début de cycle.
* La partie déterministe est donc périodique ; les parties non périodiques (variation cyclique, bruit) sont raccordées
  par un fondu à puissance constante de la queue dans la tête ; le raccord est mesuré (saut au raccord / pas typique,
  en dB : < 6 dB = inaudible).
* Normalisation commune du jeu (sonde aux points pleine charge), niveaux relatifs conservés.

## 9. Lecture en jeu (phase 8)
Rangées (état + charge) ; mélange linéaire entre rangées ; fondu à puissance constante entre régimes ;
pitch = régime / régime de la boucle ; poids lissés ; lecteurs inactifs en pause ; décélération et régénération
sur des rangées dédiées. Le même algorithme est utilisé par la couche Samples de l'application.
